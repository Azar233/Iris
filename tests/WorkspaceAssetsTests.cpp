#include "app/WorkspaceAssets.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

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

        std::filesystem::remove_all(root, cleanupError);
        std::cout << "Workspace asset catalog classification, filtering, and refresh tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::filesystem::remove_all(root, cleanupError);
        std::cerr << "Workspace asset tests failed: " << exception.what() << '\n';
        return 1;
    }
}
