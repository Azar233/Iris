#include "app/WorkspaceShaderPackage.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void write(const std::filesystem::path& file,const std::string& text){
    std::filesystem::create_directories(file.parent_path());std::ofstream(file)<<text;
}
const WorkspaceShaderFile* find(const WorkspaceShaderPackage& package,const char* name){
    for(const auto& file:package.files)if(file.path.filename()==name)return &file;
    return nullptr;
}
}
int main(){
    const auto root=std::filesystem::temp_directory_path()/ ("IrisShaderBrowser-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        write(root/"package.shaderscene",R"({"version":3,"id":"inspection","author":"Test author","license":"MIT","resourceRoot":"resources","entry":"mainImage","textures":[{"id":"Missing","encoding":"raw","path":"missing.raw"},{"id":"Noise","encoding":"hash-v1","seed":42}],"passes":[{"id":"Image","fragment":"image.frag","channels":[{"slot":1,"source":"Missing"},{"slot":2,"source":"Noise"}]}]})");
        write(root/"resources/image.frag","#include \"common.glsl\"\n#include \"absent.glsl\"\n");
        write(root/"resources/common.glsl","// shared input\n");
        write(root/"resources/unused.frag","// Not an execution input\n");
        write(root/"LICENSE.md","test license");write(root/"resources/README.md","test documentation");
        auto package=inspectWorkspaceShaderPackage(root/"package.shaderscene");
        require(package.id=="inspection" && package.author=="Test author" && package.license=="MIT","metadata lost");
        require(package.root==std::filesystem::weakly_canonical(root/"resources"),"declared resource root ignored");
        const auto* common=find(package,"common.glsl");
        require(common && !common->missing && common->sizeBytes>0 && common->owners.front()=="Pass Image","include ownership/metadata lost");
        const auto* missing=find(package,"missing.raw");
        require(missing && missing->missing && missing->owners.front().find("Channel 1")!=std::string::npos,"missing texture/channel lost");
        require(find(package,"absent.glsl") && find(package,"absent.glsl")->missing,"missing include not shown");
        require(!find(package,"unused.frag"),"directory neighbor incorrectly treated as execution input");
        require(find(package,"LICENSE.md") && !find(package,"LICENSE.md")->executionInput,"license treated as execution input");
        require(package.generatedInputs.size()==1 && package.generatedInputs.front().find("Channel 2")!=std::string::npos,"generated input lost");
        write(root/"resources/common.glsl","#include \"image.frag\"\n#include \"../../outside.glsl\"\n");
        package=inspectWorkspaceShaderPackage(root/"package.shaderscene");
        require(package.diagnostics.size()>=2 && !find(package,"outside.glsl"),"cycle or resource escape not diagnosed");
        write(root/"broken.shaderscene","{ broken");
        require(!inspectWorkspaceShaderPackage(root/"broken.shaderscene").diagnostics.empty(),"broken JSON silently accepted");
        const auto enscape=inspectWorkspaceShaderPackage(std::filesystem::path(IRIS_SOURCE_DIR)/"assets/shader-scenes/enscape/enscape.shaderscene");
        require(enscape.diagnostics.empty(),"actual Enscape inspection failed");
        for(const char* name:{"pass_a.frag","pass_b.frag","pass_c.frag","pass_image.frag","buffer_a.glsl","buffer_b.glsl","buffer_c.glsl","image.glsl","inputs.glsl","weather.raw","LICENSE.md","README.md"})require(find(enscape,name),"Enscape declared resource missing");
        const auto material=inspectWorkspaceShaderPackage(std::filesystem::path(IRIS_SOURCE_DIR)/"assets/shader-materials/checker/checker.shadermaterial");
        require(material.modelMaterial && material.manifestReadable && material.diagnostics.empty(),"Model material inspection failed");
        require(find(material,"surface.frag") && find(material,"surface.frag")->owners.front()=="Material surface", "Material source ownership lost");
        require(find(material,"LICENSE") && !find(material,"LICENSE")->executionInput,"Material license missing or executable");
        std::filesystem::remove_all(root);
        std::cout<<"Workspace ShaderScene declared roots/includes/owners/missing/cycle/escape/license/Enscape PASS\n";return 0;
    } catch(const std::exception& e){std::filesystem::remove_all(root);std::cerr<<e.what()<<'\n';return 1;}
}
