#include "plugin/RenderPluginRegistry.h"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <array>
#include <fstream>
#include <cstdint>
#include <glad/gl.h>
#include "asset/InputManifest.h"
#include "render/Renderer.h"

namespace iris {

void RenderPluginRegistry::add(RenderPluginDescriptor descriptor, Factory factory) {
    if (descriptor.id.empty() || !factory)
        throw std::invalid_argument("Render plugin requires an ID and factory");
    if (descriptor.apiVersion != renderPluginApiVersion)
        throw std::invalid_argument("Unsupported render plugin API: " + descriptor.id);
    if (contains(descriptor.id))
        throw std::invalid_argument("Duplicate render plugin ID: " + descriptor.id);
    for (const auto& service : descriptor.requiredServices)
        if (service.empty()) throw std::invalid_argument("Empty render plugin service: " + descriptor.id);
    validatePluginContract(descriptor.contract);
    if (descriptor.parameters) {
        descriptor.parameters->validate();
        if (descriptor.parameters->pluginId != descriptor.id)
            throw std::invalid_argument("Plugin parameter schema ID does not match descriptor");
    }
    entries_.push_back({std::move(descriptor), std::move(factory)});
}

bool RenderPluginRegistry::contains(const std::string& id) const {
    return std::any_of(entries_.begin(), entries_.end(), [&](const Entry& entry) {
        return entry.descriptor.id == id;
    });
}

const RenderPluginDescriptor* RenderPluginRegistry::descriptor(const std::string& id) const {
    for (const auto& entry : entries_) if (entry.descriptor.id == id) return &entry.descriptor;
    return nullptr;
}

std::unique_ptr<RenderPlugin> RenderPluginRegistry::create(const std::string& id,
    const std::vector<std::string>& services, const std::filesystem::path& shaderDirectory) const {
    const auto entry = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& value) {
        return value.descriptor.id == id;
    });
    if (entry == entries_.end()) throw std::runtime_error("Render plugin unavailable: " + id);
    for (const auto& required : entry->descriptor.requiredServices)
        if (std::find(services.begin(), services.end(), required) == services.end())
            throw std::runtime_error("Render plugin " + id + " requires service: " + required);
    auto instance = entry->factory(shaderDirectory);
    if (!instance) throw std::runtime_error("Render plugin factory returned no instance: " + id);
    return instance;
}

namespace {
// Allocation may bind textures/FBOs, but cannot publish a draw into borrowed outputs.
class PreparationBindings {
public:
    PreparationBindings() {
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active_); glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao_);
        glGetIntegerv(GL_CURRENT_PROGRAM,&program_); glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw_);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read_); glGetIntegerv(GL_RENDERBUFFER_BINDING,&renderbuffer_);
        glGetIntegerv(GL_UNPACK_ALIGNMENT,&unpack_); glGetIntegerv(GL_PACK_ALIGNMENT,&pack_);
        glGetIntegerv(GL_VIEWPORT,viewport_.data());
        GLint units=0; glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,&units);
        textures_.resize(static_cast<std::size_t>(units));
        for(GLint i=0;i<units;++i) {
            glActiveTexture(GL_TEXTURE0+i);
            glGetIntegerv(GL_TEXTURE_BINDING_2D,&textures_[i][0]);
            glGetIntegerv(GL_TEXTURE_BINDING_3D,&textures_[i][1]);
            glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP,&textures_[i][2]);
        }
        glActiveTexture(active_);
    }
    ~PreparationBindings() {
        for(std::size_t i=0;i<textures_.size();++i) {
            glActiveTexture(GL_TEXTURE0+static_cast<GLenum>(i));
            glBindTexture(GL_TEXTURE_2D,textures_[i][0]); glBindTexture(GL_TEXTURE_3D,textures_[i][1]);
            glBindTexture(GL_TEXTURE_CUBE_MAP,textures_[i][2]);
        }
        glActiveTexture(active_); glBindVertexArray(vao_); glUseProgram(program_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw_); glBindFramebuffer(GL_READ_FRAMEBUFFER,read_);
        glBindRenderbuffer(GL_RENDERBUFFER,renderbuffer_);
        glPixelStorei(GL_UNPACK_ALIGNMENT,unpack_); glPixelStorei(GL_PACK_ALIGNMENT,pack_);
        glViewport(viewport_[0],viewport_[1],viewport_[2],viewport_[3]);
    }
private:
    GLint active_{0},vao_{0},program_{0},draw_{0},read_{0},renderbuffer_{0},unpack_{4},pack_{4};
    std::array<GLint,4> viewport_{};
    std::vector<std::array<GLint,3>> textures_;
};
struct ResourceInput {
    std::filesystem::path path;
    std::filesystem::file_time_type time;
    std::uintmax_t size;
    std::uint64_t hash;
};
ResourceInput inputIdentity(const std::filesystem::path& path) {
    if(!std::filesystem::is_regular_file(path)) throw std::runtime_error("Plugin resource missing: "+path.u8string());
    ResourceInput input{path,std::filesystem::last_write_time(path),std::filesystem::file_size(path),14695981039346656037ULL};
    std::ifstream stream(path,std::ios::binary);
    if(!stream) throw std::runtime_error("Cannot read plugin resource: "+path.u8string());
    std::array<char,16384> block{};
    while(stream) {
        stream.read(block.data(),static_cast<std::streamsize>(block.size()));
        for(std::streamsize i=0;i<stream.gcount();++i) input.hash=(input.hash^static_cast<unsigned char>(block[i]))*1099511628211ULL;
    }
    if(!stream.eof()) throw std::runtime_error("Plugin resource read failed: "+path.u8string());
    capture::recordInput(path);
    return input;
}
}
std::unique_ptr<RenderPlugin> RenderPluginRegistry::prepareReplacement(const std::string& id,
    const std::vector<std::string>& services, const std::filesystem::path& directory,
    const RendererSettings& settings, int width, int height, const std::function<bool()>& stillCurrent) const {
    if(width<1 || height<1) throw std::invalid_argument("Invalid plugin preparation size");
    if(stillCurrent && !stillCurrent()) throw std::runtime_error("Plugin resource transaction canceled or stale");
    if (!glGetString || !glGetString(GL_VERSION)) throw std::runtime_error("Plugin resource preparation requires an OpenGL context");
    const auto* description=descriptor(id);
    if(!description) throw std::runtime_error("Render plugin unavailable: "+id);
    std::vector<ResourceInput> inputs;
    if(description->parameters) {
        // Validate effective values without altering the authored settings.
        auto candidate=settings; bool history=false; std::string error;
        const auto values=description->parameters->capture(settings);
        if(!description->parameters->apply(candidate,values,history,error)) throw std::runtime_error(error);
        for(const auto& value:values) if(value.value.type==ModuleParameterType::Asset && !value.value.text.empty())
            inputs.push_back(inputIdentity(std::filesystem::u8path(value.value.text)));
    }
    if(glGetError()!=GL_NO_ERROR) throw std::runtime_error("GL error before plugin resource preparation");
    PreparationBindings bindings;
    auto candidate=create(id,services,directory);
    candidate->prepareResources(settings,width,height);
    candidate->invalidateHistory();
    if(glGetError()!=GL_NO_ERROR) throw std::runtime_error("GL error preparing plugin resources: "+id);
    for(const auto& input:inputs) {
        const auto current=inputIdentity(input.path);
        if(input.time!=current.time || input.size!=current.size || input.hash!=current.hash)
            throw std::runtime_error("Plugin resource changed during preparation: "+input.path.u8string());
    }
    if(stillCurrent && !stillCurrent()) throw std::runtime_error("Plugin resource transaction canceled or stale");
    return candidate;
}

} // namespace iris
