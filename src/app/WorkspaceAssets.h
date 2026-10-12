#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

enum class WorkspaceAssetCategory {
    Scenes = 0,
    Models,
    Materials,
    Textures,
    Hdri,
    Modules,
    Simulations,
    Caches,
    RenderJobs,
    Presets,
    ShaderScenes,
    Shaders,
    Count
};

enum class WorkspaceAssetSort {
    Name = 0,
    Size
};

struct WorkspaceAssetRecord {
    WorkspaceAssetCategory category{WorkspaceAssetCategory::Scenes};
    std::filesystem::path path;
    std::filesystem::path relativePath;
    std::string displayName;
    std::string extension;
    std::uintmax_t sizeBytes{0U};
    std::uint64_t previewCacheKey{0U};
};

const char* workspaceAssetCategoryName(WorkspaceAssetCategory category);
const char* workspaceAssetCategoryBadge(WorkspaceAssetCategory category);

class WorkspaceAssetCatalog final {
  public:
    bool refresh(const std::filesystem::path& sourceRoot, std::string& error);

    std::uint64_t generation() const { return generation_; }
    const std::vector<WorkspaceAssetRecord>& records() const { return records_; }
    std::size_t count(WorkspaceAssetCategory category) const;
    const WorkspaceAssetRecord* find(const std::filesystem::path& path) const;
    std::vector<const WorkspaceAssetRecord*> filter(
        WorkspaceAssetCategory category,
        const std::string& query = {},
        const std::string& extension = {},
        WorkspaceAssetSort sort = WorkspaceAssetSort::Name
    ) const;
    std::vector<std::string> extensions(WorkspaceAssetCategory category) const;

  private:
    std::vector<WorkspaceAssetRecord> records_;
    std::uint64_t generation_{0U};
};
