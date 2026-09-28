#include "app/WorkspaceAssets.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <initializer_list>
#include <set>
#include <system_error>

namespace {

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool endsWith(const std::string& value, const char* suffix) {
    const std::size_t suffixLength = std::char_traits<char>::length(suffix);
    return value.size() >= suffixLength
        && value.compare(value.size() - suffixLength, suffixLength, suffix) == 0;
}

bool containsInsensitive(const std::string& value, const std::string& query) {
    if (query.empty()) return true;
    return lowercase(value).find(lowercase(query)) != std::string::npos;
}

bool isOneOf(const std::string& extension, const std::initializer_list<const char*> values) {
    return std::any_of(values.begin(), values.end(), [&](const char* value) {
        return extension == value;
    });
}

bool classify(const std::filesystem::path& relativePath, WorkspaceAssetCategory& category) {
    const std::string generic = lowercase(relativePath.generic_u8string());
    const std::string extension = lowercase(relativePath.extension().string());
    if (extension == ".myscene") category = WorkspaceAssetCategory::Scenes;
    else if (extension == ".renderjob") category = WorkspaceAssetCategory::RenderJobs;
    else if (endsWith(generic, ".module.json")) category = WorkspaceAssetCategory::Modules;
    else if (endsWith(generic, ".simulation.json")) category = WorkspaceAssetCategory::Simulations;
    else if (isOneOf(extension, {".simcache", ".cache"})) category = WorkspaceAssetCategory::Caches;
    else if (isOneOf(extension, {".preset", ".cube"})) category = WorkspaceAssetCategory::Presets;
    else if (isOneOf(extension, {".hdr", ".exr"})) category = WorkspaceAssetCategory::Hdri;
    else if (isOneOf(extension, {".obj", ".dae", ".gltf", ".glb"})) {
        category = WorkspaceAssetCategory::Models;
    } else if (extension == ".mtl") category = WorkspaceAssetCategory::Materials;
    else if (isOneOf(extension, {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".ppm"})) {
        category = WorkspaceAssetCategory::Textures;
    } else {
        return false;
    }
    return true;
}

bool isInternalSceneFixture(const std::filesystem::path& relativePath) {
    const std::string generic = lowercase(relativePath.generic_u8string());
    constexpr const char* fixturePrefix = "assets/scenes/fixtures/";
    if (generic.rfind(fixturePrefix, 0U) == 0U) return true;

    // copy_directory intentionally does not remove files from an existing build tree. Keep an
    // upgraded editor from rediscovering the former top-level fixture copies until that build tree
    // is rebuilt from scratch.
    static constexpr std::array<const char*, 22> migratedFixtureNames{{
        "01_multi_model_hierarchy.myscene",
        "02_pbr_materials.myscene",
        "03_deferred_ssao_taa.myscene",
        "04_volume_glass.myscene",
        "05_glass_caustics.myscene",
        "06_prism_spectrum.myscene",
        "07_local_lights.myscene",
        "08_instancing_lod.myscene",
        "09_gpu_animation.myscene",
        "10_reference_pathtracer_pbr_hdri.myscene",
        "11_reference_pathtracer_lights.myscene",
        "12_reference_pathtracer_volume.myscene",
        "13_polyhaven_studio_lounge.myscene",
        "14_polyhaven_material_gallery.myscene",
        "15_stylized_clean_toon_gallery.myscene",
        "16_stylized_painterly_interior.myscene",
        "17_stylized_night_aurora_outdoor.myscene",
        "18_atmosphere_sky.myscene",
        "19_coastal_cascades.myscene",
        "20_ocean_synthesis.myscene",
        "21_ocean_depth.myscene",
        "22_ocean_underwater.myscene"
    }};
    const std::string filename = lowercase(relativePath.filename().string());
    const std::filesystem::path parent = relativePath.parent_path();
    return lowercase(parent.generic_u8string()) == "assets/scenes"
        && std::find(migratedFixtureNames.begin(), migratedFixtureNames.end(), filename)
            != migratedFixtureNames.end();
}

void hashBytes(std::uint64_t& hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0U; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ULL;
    }
}

std::uint64_t previewCacheKey(const std::filesystem::path& relativePath,
                              std::uintmax_t sizeBytes,
                              const std::filesystem::file_time_type& modified) {
    std::uint64_t hash = 1469598103934665603ULL;
    const std::string path = relativePath.generic_u8string();
    hashBytes(hash, path.data(), path.size());
    hashBytes(hash, &sizeBytes, sizeof(sizeBytes));
    const auto ticks = modified.time_since_epoch().count();
    hashBytes(hash, &ticks, sizeof(ticks));
    return hash;
}

} // namespace

const char* workspaceAssetCategoryName(WorkspaceAssetCategory category) {
    switch (category) {
        case WorkspaceAssetCategory::Scenes: return "Scenes";
        case WorkspaceAssetCategory::Models: return "Models";
        case WorkspaceAssetCategory::Materials: return "Materials";
        case WorkspaceAssetCategory::Textures: return "Textures";
        case WorkspaceAssetCategory::Hdri: return "HDRI";
        case WorkspaceAssetCategory::Modules: return "Modules";
        case WorkspaceAssetCategory::Simulations: return "Simulations";
        case WorkspaceAssetCategory::Caches: return "Caches";
        case WorkspaceAssetCategory::RenderJobs: return "RenderJobs";
        case WorkspaceAssetCategory::Presets: return "Presets";
        case WorkspaceAssetCategory::Count: break;
    }
    return "Unknown";
}

const char* workspaceAssetCategoryBadge(WorkspaceAssetCategory category) {
    switch (category) {
        case WorkspaceAssetCategory::Scenes: return "SCENE";
        case WorkspaceAssetCategory::Models: return "MODEL";
        case WorkspaceAssetCategory::Materials: return "MAT";
        case WorkspaceAssetCategory::Textures: return "TEX";
        case WorkspaceAssetCategory::Hdri: return "HDRI";
        case WorkspaceAssetCategory::Modules: return "MODULE";
        case WorkspaceAssetCategory::Simulations: return "SIM";
        case WorkspaceAssetCategory::Caches: return "CACHE";
        case WorkspaceAssetCategory::RenderJobs: return "JOB";
        case WorkspaceAssetCategory::Presets: return "PRESET";
        case WorkspaceAssetCategory::Count: break;
    }
    return "ASSET";
}

bool WorkspaceAssetCatalog::refresh(const std::filesystem::path& sourceRoot,
                                    std::string& error) {
    try {
        const std::filesystem::path assetRoot = sourceRoot / "assets";
        if (!std::filesystem::is_directory(assetRoot)) {
            error = "Workspace asset root does not exist: " + assetRoot.string();
            return false;
        }

        std::vector<WorkspaceAssetRecord> candidate;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 assetRoot, std::filesystem::directory_options::skip_permission_denied)) {
            if (!entry.is_regular_file()) continue;
            const std::filesystem::path relative = entry.path().lexically_relative(sourceRoot);
            // Stage and regression scenes remain directly loadable by the acceptance tools, but
            // they are implementation fixtures rather than projects a user should choose from the
            // Content Browser or File > Open bundled scene.
            if (isInternalSceneFixture(relative)) continue;
            WorkspaceAssetCategory category;
            if (!classify(relative, category)) continue;
            std::error_code sizeError;
            const std::uintmax_t size = entry.file_size(sizeError);
            std::error_code timeError;
            const auto modified = entry.last_write_time(timeError);
            candidate.push_back(WorkspaceAssetRecord{
                category,
                entry.path().lexically_normal(),
                relative,
                entry.path().filename().string(),
                lowercase(entry.path().extension().string()),
                sizeError ? 0U : size,
                previewCacheKey(relative, sizeError ? 0U : size,
                                timeError ? std::filesystem::file_time_type{} : modified)
            });
        }
        std::sort(candidate.begin(), candidate.end(), [](const auto& left, const auto& right) {
            if (left.category != right.category) return left.category < right.category;
            return lowercase(left.relativePath.generic_u8string())
                < lowercase(right.relativePath.generic_u8string());
        });
        records_ = std::move(candidate);
        ++generation_;
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = "Workspace asset refresh failed: " + std::string(exception.what());
        return false;
    }
}

std::size_t WorkspaceAssetCatalog::count(WorkspaceAssetCategory category) const {
    return static_cast<std::size_t>(std::count_if(records_.begin(), records_.end(),
        [category](const auto& record) { return record.category == category; }));
}

const WorkspaceAssetRecord* WorkspaceAssetCatalog::find(const std::filesystem::path& path) const {
    const std::filesystem::path normalized = path.lexically_normal();
    const auto found = std::find_if(records_.begin(), records_.end(), [&](const auto& record) {
        return record.path == normalized;
    });
    return found == records_.end() ? nullptr : &*found;
}

std::vector<const WorkspaceAssetRecord*> WorkspaceAssetCatalog::filter(
    WorkspaceAssetCategory category,
    const std::string& query,
    const std::string& extension,
    WorkspaceAssetSort sort
) const {
    std::vector<const WorkspaceAssetRecord*> result;
    const std::string normalizedExtension = lowercase(extension);
    for (const auto& record : records_) {
        if (record.category != category) continue;
        if (!normalizedExtension.empty() && record.extension != normalizedExtension) continue;
        if (!containsInsensitive(record.displayName, query)
            && !containsInsensitive(record.relativePath.generic_u8string(), query)) continue;
        result.push_back(&record);
    }
    std::sort(result.begin(), result.end(), [sort](const auto* left, const auto* right) {
        if (sort == WorkspaceAssetSort::Size && left->sizeBytes != right->sizeBytes) {
            return left->sizeBytes > right->sizeBytes;
        }
        return lowercase(left->relativePath.generic_u8string())
            < lowercase(right->relativePath.generic_u8string());
    });
    return result;
}

std::vector<std::string> WorkspaceAssetCatalog::extensions(
    WorkspaceAssetCategory category
) const {
    std::set<std::string> unique;
    for (const auto& record : records_) {
        if (record.category == category) unique.emplace(record.extension);
    }
    return {unique.begin(), unique.end()};
}
