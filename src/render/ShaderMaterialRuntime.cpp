#include "render/ShaderMaterialRuntime.h"
#include "render/Shader.h"
#include "render/Renderer.h"
#include "render/GpuModel.h"
#include "render/Camera.h"
#include "asset/InputManifest.h"
#include <glad/gl.h>
#include <set>
#include <algorithm>
#include <stdexcept>
#include <sstream>
#include <iomanip>

struct ShaderMaterialRuntime::Entry {
    iris::ShaderScenePackage package;
    std::unique_ptr<Shader> shader;
    std::vector<capture::InputStamp> inputs;
};
ShaderMaterialRuntime::ShaderMaterialRuntime(std::filesystem::path shaders):vertex_(shaders/"basic.vert"){}
ShaderMaterialRuntime::~ShaderMaterialRuntime()=default;
const iris::ShaderScenePackage* ShaderMaterialRuntime::package(const std::string& resource) const {
    const auto found=entries_.find(resource);return found==entries_.end()?nullptr:&found->second->package;
}
bool ShaderMaterialRuntime::prepare(const std::vector<RenderItem>& items,const RendererSettings& settings,bool retainFailure,bool retry,bool pollInputs) {
    std::set<std::string> requested;
    std::ostringstream signature;signature<<std::setprecision(9);
    for(const auto& item:items)if(!item.shaderMaterial.resource.empty()) {
        iris::validateShaderMaterialSettings(settings);
        if(!item.model || item.model->hasSkinning() || item.model->transparentSubmeshCount())
            throw std::runtime_error("ShaderMaterial supports static opaque models only");
        for(const auto& material:item.model->sourceData()->materials)
            if(material.alphaMode!=MaterialAlphaMode::Opaque || material.transmissionFactor>0)
                throw std::runtime_error("ShaderMaterial rejects masked/blended/transmissive materials");
        requested.insert(item.shaderMaterial.resource);
        signature<<item.entityId<<':'<<item.shaderMaterial.resource<<';';
        for(const auto& parameter:item.shaderMaterial.parameters)signature<<parameter.id<<':'<<parameter.value.number<<';';
    }
    if(requested.size()>32)throw std::runtime_error("ShaderMaterial supports at most 32 active packages");
    const bool inputsChanged=pollInputs && std::any_of(watched_.begin(),watched_.end(),[](const auto& stamp){return stamp.changed();});
    bool keysChanged=requested.size()!=entries_.size();
    for(const auto& key:requested)keysChanged=keysChanged || !entries_.count(key);
    if(!retry && !keysChanged && !inputsChanged) {
        for(const auto& item:items)if(!item.shaderMaterial.resource.empty())iris::shaderMaterialValues(*package(item.shaderMaterial.resource),item.shaderMaterial);
        const bool changed=valuesKey_!=signature.str();valuesKey_=signature.str();return changed;
    }
    std::map<std::string,std::unique_ptr<Entry>> candidates;
    capture::InputWatch watch;
    try {
        for(const auto& key:requested) {
            auto entry=std::make_unique<Entry>();capture::InputWatch packageWatch;
            std::string message;
            if(!iris::loadShaderScenePackage(std::filesystem::u8path(key),entry->package,message))throw std::runtime_error(message);
            for(const auto& item:items)if(item.shaderMaterial.resource==key)iris::shaderMaterialValues(entry->package,item.shaderMaterial);
            Shader::SourcePolicy policy;policy.fragmentRoot=entry->package.root;policy.packageOwnedReload=true;
            policy.passLabel="Material "+entry->package.id;
            policy.uniforms={{"uInstanced",GL_BOOL},{"uSkinningEnabled",GL_BOOL},{"uHasNormalTexture",GL_BOOL},{"uModel",GL_FLOAT_MAT4},{"uNodeTransform",GL_FLOAT_MAT4},{"uView",GL_FLOAT_MAT4},{"uProjection",GL_FLOAT_MAT4},
                {"uBaseColor",GL_FLOAT_VEC4},{"uBaseColorTexture",GL_SAMPLER_2D},{"uNormalTexture",GL_SAMPLER_2D},
                {"uMetallicRoughnessTexture",GL_SAMPLER_2D},{"uMetallicFactor",GL_FLOAT},{"uRoughnessFactor",GL_FLOAT},
                {"uCameraPosition",GL_FLOAT_VEC3},{"irisTime",GL_FLOAT},{"irisResolution",GL_FLOAT_VEC2}};
            for(const auto& parameter:entry->package.parameters.descriptors())policy.uniforms.emplace_back(parameter.id,GL_FLOAT);
            capture::watchInput(vertex_);
            entry->shader=std::make_unique<Shader>(vertex_,entry->package.passes.front().fragment,std::move(policy));
            entry->inputs=packageWatch.stamps;
            candidates.emplace(key,std::move(entry));
        }
        for(const auto& stamp:watch.stamps)if(stamp.changed())throw std::runtime_error("ShaderMaterial input changed during preparation");
        watched_=std::move(watch.stamps);entries_.swap(candidates);error_.clear();valuesKey_=signature.str();return true;
    }catch(const std::exception& exception) {
        error_=exception.what();
        if(retainFailure && !keysChanged) {
            for(const auto& stamp:watched_)watch.record(stamp.path);
            watched_=std::move(watch.stamps);
            for(const auto& item:items)if(!item.shaderMaterial.resource.empty())iris::shaderMaterialValues(*package(item.shaderMaterial.resource),item.shaderMaterial);
            return false;
        }
        throw;
    }
}
Shader& ShaderMaterialRuntime::bind(const RenderItem& item,const Camera& camera,const RendererSettings& settings,int width,int height) {
    auto& entry=*entries_.at(item.shaderMaterial.resource);
    if(capture::activeManifest) {
        for(const auto& input:entry.inputs)if(input.changed())throw std::runtime_error("ShaderMaterial input changed after preparation");
        capture::recordInput(entry.package.manifest);
    }
    auto& shader=*entry.shader;shader.use();
    shader.setMat4("uModel",item.modelMatrix);shader.setMat4("uView",camera.viewMatrix());
    shader.setMat4("uProjection",camera.projectionMatrix(static_cast<float>(width)/height));shader.setBool("uInstanced",false);
    shader.setVec3("uCameraPosition",camera.position());shader.setFloat("irisTime",settings.water.timeSeconds);
    shader.setVec2("irisResolution",glm::vec2(width,height));
    const auto values=iris::shaderMaterialValues(entry.package,item.shaderMaterial);
    const auto& descriptors=entry.package.parameters.descriptors();
    for(std::size_t i=0;i<values.size();++i)shader.setFloat(descriptors[i].id.c_str(),values[i].number);
    return shader;
}
