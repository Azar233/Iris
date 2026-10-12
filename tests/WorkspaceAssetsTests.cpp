#include "app/WorkspaceAssets.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void writeFixture(const std::filesystem::path& path, const std::string& text = "fixture") {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    require(static_cast<bool>(output), "failed to write Workspace asset fixture");
}
#ifdef _WIN32
std::filesystem::path shortAlias(const std::filesystem::path& path) {
    std::wstring buffer(32768,L'\0');
    const auto length=GetShortPathNameW(path.c_str(),buffer.data(),static_cast<DWORD>(buffer.size()));
    if (!length || length>=buffer.size()) return path; // Volume may not provide 8.3 aliases.
    buffer.resize(length);return std::filesystem::path(buffer);
}
#endif

} // namespace

int main() {
    const std::filesystem::path root = std::filesystem::temp_directory_path()
        / "MyRendererWorkspaceAssetsTests";
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    try {
        writeFixture(root / "assets" / "scenes" / "hero.myscene");
        writeFixture(root / "assets" / "scenes" / "fixtures" / "internal.myscene");
        writeFixture(root / "assets" / "scenes" / "01_multi_model_hierarchy.myscene");
        writeFixture(root / "assets" / "models" / "nested" / "hero.gltf", "model");
        writeFixture(root / "assets" / "models" / "nested" / "hero.mtl");
        writeFixture(root / "assets" / "models" / "nested" / "hero_diffuse.png", "texture");
        writeFixture(root / "assets" / "environments" / "studio.exr", "environment");
        writeFixture(root / "assets" / "renderjobs" / "hero.renderjob");
        writeFixture(root / "assets" / "modules" / "ocean.module.json");
        writeFixture(root / "assets" / "simulations" / "waves.simulation.json");
        writeFixture(root / "assets" / "caches" / "waves.simcache");
        writeFixture(root / "assets" / "presets" / "night.cube");
        writeFixture(root / "assets" / "ignored" / "notes.txt");

        WorkspaceAssetCatalog catalog;
        std::string error;
        require(catalog.refresh(root, error), error.c_str());
        require(catalog.generation() == 1U, "first refresh did not advance generation");
        require(catalog.records().size() == 10U, "catalog classified an unexpected asset count");
        require(catalog.count(WorkspaceAssetCategory::Scenes) == 1U,
                "visible scene classification included an internal fixture");
        require(catalog.find(root / "assets" / "scenes" / "fixtures" / "internal.myscene") == nullptr,
                "internal scene fixture was exposed by the workspace catalog");
        require(catalog.find(root / "assets" / "scenes" / "01_multi_model_hierarchy.myscene") == nullptr,
                "stale pre-migration scene fixture was exposed by the workspace catalog");
        require(catalog.count(WorkspaceAssetCategory::Models) == 1U,
                "recursive model asset was not classified");
        require(catalog.count(WorkspaceAssetCategory::Textures) == 1U,
                "texture asset was not classified");
        require(catalog.count(WorkspaceAssetCategory::Hdri) == 1U,
                "HDRI asset was not classified");
        require(catalog.count(WorkspaceAssetCategory::RenderJobs) == 1U,
                "Render Job asset was not classified");

        const auto modelSearch = catalog.filter(
            WorkspaceAssetCategory::Models, "NESTED/HERO", ".gltf");
        require(modelSearch.size() == 1U
                && modelSearch.front()->displayName == "hero.gltf",
                "case-insensitive path search or extension filter failed");
        require(modelSearch.front()->sizeBytes == 5U,
                "asset size metadata was not recorded");
        require(modelSearch.front()->previewCacheKey != 0U,
                "stable preview cache key was not generated");
        require(catalog.find(modelSearch.front()->path) == modelSearch.front(),
                "catalog path lookup failed");
#ifdef _WIN32
        require(catalog.find(shortAlias(modelSearch.front()->path))==modelSearch.front(),
                "native asset short-path lookup lost original record");
#endif

        const auto modelExtensions = catalog.extensions(WorkspaceAssetCategory::Models);
        require(modelExtensions.size() == 1U && modelExtensions.front() == ".gltf",
                "category extension inventory is incorrect");

        writeFixture(root / "assets" / "models" / "large.obj", std::string(32U, 'x'));
        require(catalog.refresh(root, error), error.c_str());
        const auto sizeSorted = catalog.filter(
            WorkspaceAssetCategory::Models, {}, {}, WorkspaceAssetSort::Size);
        require(catalog.generation() == 2U && sizeSorted.size() == 2U
                && sizeSorted.front()->displayName == "large.obj",
                "refresh generation or size sort failed");

        const std::size_t preservedCount = catalog.records().size();
        require(!catalog.refresh(root / "missing", error),
                "missing asset root unexpectedly refreshed");
        require(catalog.records().size() == preservedCount && catalog.generation() == 2U,
                "failed refresh replaced the last valid catalog");

        writeFixture(root / "assets/shader-scenes/custom/custom.shaderscene");
        writeFixture(root / "assets/shader-scenes/custom/resources/image.frag");
        writeFixture(root / "shaders/packages/external-root/scene.shaderscene");
        writeFixture(root / "shaders/packages/external-root/nested/input.glsl");
        require(catalog.refresh(root,error),error.c_str());
        require(catalog.count(WorkspaceAssetCategory::ShaderScenes)==2U,
                "ShaderScene category omitted project shader root manifests");
        require(catalog.count(WorkspaceAssetCategory::Shaders)==2U,
                "Shader category omitted declared package root or native GLSL");
        require(catalog.filter(WorkspaceAssetCategory::Shaders,"EXTERNAL-ROOT").size()==1U,
                "shader path search did not cover resources outside assets");

        const auto project=root/"isolated-project";
        std::filesystem::create_directories(project/"assets");
        const auto external=root/std::filesystem::u8path(u8"外部包 空间");
        const auto manifest=external/"scene.shaderscene";
        const auto include=external/std::filesystem::u8path(u8"resources/nested/细节.glsl");
        const std::string description=R"({"version":3,"id":"external","resourceRoot":"resources","entry":"main","camera":"project","author":"test","license":"MIT","parameters":[],"passes":[{"id":"Image","fragment":"image.frag","output":"display-rgba8","sizeDivisor":1,"channels":[{"slot":0,"source":"Raw","frame":"current","filter":"nearest","wrap":"clamp","mip":"none"}]}],"textures":[{"id":"Raw","dimension":"2d","format":"linear-r8","extent":[1,1],"encoding":"raw","path":"input.raw","mip":"none"}]})";
        writeFixture(manifest,description);
        writeFixture(external/"resources/image.frag",u8"#include \"nested/细节.glsl\"\n");
        writeFixture(include);writeFixture(external/"resources/input.raw","x");
        writeFixture(external/"README.md");writeFixture(external/"resources/unused.frag");writeFixture(external/"unrelated.obj");
        WorkspaceAssetCatalog externalCatalog;
        require(externalCatalog.refresh(project,error),error.c_str());
        require(externalCatalog.registerShaderPackage(project,manifest,error),error.c_str());
        require(externalCatalog.registeredShaderPackageCount()==1 && externalCatalog.records().size()==4,"External declared input registration failed");
        require(externalCatalog.find(include) && externalCatalog.find(include)->displayName==u8"细节.glsl","Unicode source identity lost");
#ifdef _WIN32
        require(externalCatalog.find(shortAlias(include))==externalCatalog.find(include),"external include short-path identity lost");
        require(externalCatalog.registerShaderPackage(project,shortAlias(manifest),error)
            && externalCatalog.registeredShaderPackageCount()==1,"short-path registration duplicated package");
        std::cout<<"Windows 8.3 path alias lookup and registration PASS (where aliases are available)\n";
#endif
        require(!externalCatalog.find(external/"resources/unused.frag") && !externalCatalog.find(external/"unrelated.obj"),"Unreferenced external files were indexed");
        require(externalCatalog.registerShaderPackage(project,external/"resources/../scene.shaderscene",error)
            && externalCatalog.registeredShaderPackageCount()==1 && externalCatalog.records().size()==4,"Alias registration duplicated files");
        const auto generation=externalCatalog.generation();
        require(!externalCatalog.registerShaderPackage(project,external/"missing.shaderscene",error)
            && externalCatalog.generation()==generation && externalCatalog.registeredShaderPackageCount()==1,"Rejected registration changed catalog");
        std::filesystem::remove(external/"resources/input.raw");
#ifdef _WIN32
        require(externalCatalog.find(shortAlias(external)/"resources/input.raw")==externalCatalog.find(external/"resources/input.raw"),
                "missing leaf changed cached alias identity before refresh");
#endif
        require(externalCatalog.refresh(project,error) && externalCatalog.find(external/"resources/input.raw")->missing,"Missing declared texture was discarded");
        writeFixture(manifest,"{");
        require(externalCatalog.refresh(project,error) && externalCatalog.find(include),"Malformed Manifest lost known dependencies");
        std::filesystem::remove(manifest);
        require(externalCatalog.refresh(project,error) && externalCatalog.find(manifest)->missing && externalCatalog.find(include),"Missing Manifest lost package identity");
        writeFixture(manifest,description);writeFixture(external/"resources/input.raw","restored");
        require(externalCatalog.refresh(project,error) && !externalCatalog.find(manifest)->missing
            && externalCatalog.find(external/"resources/input.raw")->sizeBytes==8,"External recovery not reflected");
        writeFixture(external/"alias.shaderscene",description);
        require(externalCatalog.registerShaderPackage(project,external/"alias.shaderscene",error)
            && externalCatalog.records().size()==5,"Shared dependencies duplicated catalog rows");
        const auto other=external/"other";
        writeFixture(other/"scene.shaderscene",description);writeFixture(other/"resources/image.frag","void main(){}\n");writeFixture(other/"resources/input.raw","x");
        require(externalCatalog.registerShaderPackage(project,other/"scene.shaderscene",error),error.c_str());
        require(externalCatalog.filter(WorkspaceAssetCategory::ShaderScenes,"scene.shaderscene").size()==2
            && externalCatalog.registeredShaderPackageCount()==3,"Same-name packages merged identities");
        require(!externalCatalog.refresh(project/"missing",error) && externalCatalog.registeredShaderPackageCount()==3,"Failed refresh changed registrations");
        std::cout<<"External packages / Unicode / declared-only / alias+shared identity / missing+malformed recovery / same-name / transactional refresh PASS\n";
        std::filesystem::remove_all(root, cleanupError);
        std::cout << "Workspace asset catalog classification, filtering, and refresh tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::filesystem::remove_all(root, cleanupError);
        std::cerr << "Workspace asset tests failed: " << exception.what() << '\n';
        return 1;
    }
}
