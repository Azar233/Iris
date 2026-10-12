#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>
#include "app/WorkspaceShaderPackage.h"

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
    bool missing{false};
};

const char* workspaceAssetCategoryName(WorkspaceAssetCategory category);
const char* workspaceAssetCategoryBadge(WorkspaceAssetCategory category);

class WorkspaceAssetCatalog final {
  public:
    bool refresh(const std::filesystem::path& sourceRoot, std::string& error);
    // Session-only registration after the application's successful package publication.
    // Index declared inputs only; failure preserves both registrations and records.
    bool registerShaderPackage(const std::filesystem::path& sourceRoot,
        const std::filesystem::path& manifest, std::string& error);
    std::size_t registeredShaderPackageCount() const { return shaderPackages_.size(); }

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
    std::unordered_map<std::string,std::size_t> canonicalIndex_;
    std::vector<WorkspaceShaderPackage> shaderPackages_;
    std::uint64_t generation_{0U};
};
