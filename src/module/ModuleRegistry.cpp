#include "module/ModuleRegistry.h"

#include <algorithm>
#include <utility>

// Injected by CMake so every module manifest in one build reports the same id, and
// a rebuilt binary cannot silently reuse a cache entry produced by an older one.
const std::string& moduleBuildId() {
#ifdef MYRENDERER_BUILD_ID
    static const std::string buildId{MYRENDERER_BUILD_ID};
#else
    static const std::string buildId{"unspecified"};
#endif
    return buildId;
}

const char* moduleKindName(ModuleKind kind) {
    switch (kind) {
        case ModuleKind::Scene: return "Scene";
        case ModuleKind::Simulation: return "Simulation";
    }
    return "Unknown";
}

const char* moduleLogSeverityName(ModuleLogSeverity severity) {
    switch (severity) {
        case ModuleLogSeverity::Info: return "Info";
        case ModuleLogSeverity::Warning: return "Warning";
        case ModuleLogSeverity::Error: return "Error";
    }
    return "Unknown";
}

SceneContext::SceneContext(
    RuntimeScene& runtimeScene,
    const Timeline& timeline,
    ParameterRegistry& parameters,
    std::uint32_t seed
) : runtimeScene_(&runtimeScene),
    timeline_(&timeline),
    parameters_(&parameters),
    seed_(seed) {}

void SceneContext::log(ModuleLogSeverity severity, std::string message) const {
    logEntries_.push_back(ModuleLogEntry{
        severity, timeline_ != nullptr ? timeline_->frame() : 0, std::move(message)
    });
}

bool SceneContext::hasErrors() const {
    return std::any_of(logEntries_.begin(), logEntries_.end(), [](const ModuleLogEntry& entry) {
        return entry.severity == ModuleLogSeverity::Error;
    });
}

bool ModuleRegistry::add(ModuleManifest manifest, Factory factory, std::string& error) {
    if (manifest.id.empty()) {
        error = "module manifest id must not be empty";
        return false;
    }
    if (manifest.apiVersion != moduleApiVersion) {
        error = "module '" + manifest.id + "' targets API version "
            + std::to_string(manifest.apiVersion) + ", runtime provides "
            + std::to_string(moduleApiVersion);
        return false;
    }
    if (!factory) {
        error = "module '" + manifest.id + "' has no factory";
        return false;
    }
    if (contains(manifest.id)) {
        error = "module id '" + manifest.id + "' is already registered";
        return false;
    }
    if (manifest.buildId.empty()) manifest.buildId = moduleBuildId();
    entries_.push_back(Entry{std::move(manifest), std::move(factory)});
    return true;
}

bool ModuleRegistry::contains(const std::string& id) const {
    return find(id) != nullptr;
}

std::vector<ModuleManifest> ModuleRegistry::manifests() const {
    std::vector<ModuleManifest> result;
    result.reserve(entries_.size());
    for (const Entry& entry : entries_) result.push_back(entry.manifest);
    std::sort(result.begin(), result.end(), [](const ModuleManifest& left, const ModuleManifest& right) {
        return left.id < right.id;
    });
    return result;
}

const ModuleManifest* ModuleRegistry::find(const std::string& id) const {
    for (const Entry& entry : entries_) {
        if (entry.manifest.id == id) return &entry.manifest;
    }
    return nullptr;
}

std::unique_ptr<ISceneModule> ModuleRegistry::create(
    const std::string& id,
    std::string& error
) const {
    for (const Entry& entry : entries_) {
        if (entry.manifest.id != id) continue;
        std::unique_ptr<ISceneModule> module = entry.factory();
        if (!module) {
            error = "module '" + id + "' factory returned no instance";
            return nullptr;
        }
        return module;
    }
    error = id == rigidBodyModuleId
        ? "Physics module unavailable: rebuild Iris with IRIS_ENABLE_PHYSX=ON (Windows/MSVC x64)"
        : "unknown module id '" + id + "'";
    return nullptr;
}
