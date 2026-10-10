#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "module/RuntimeScene.h"

// Deterministic simulation cache.
//
// A bake writes the module's per-frame results together with the exact inputs that
// produced them. A render may reuse a cached frame only when every input matches:
// a differing scene hash, module id, module API version, build id, seed, frame rate
// or frame range makes the entry *stale*, and a stale entry is reported instead of
// being silently reused. Every frame also carries the content hash it produced, so a
// reused frame is verified rather than trusted.

// The frame rate is part of the key instead of the derived fixed step: comparing an
// integer is exact, while comparing a double step invites "almost equal" bugs.
struct SimulationCacheKey {
    std::uint64_t sceneContentHash{0U};
    std::string moduleId;
    int moduleApiVersion{1};
    std::string buildId;
    std::uint32_t seed{0U};
    // Fingerprint of the module's effective parameter values. Without it, editing a
    // parameter would silently reuse an entry produced by other inputs: the per-frame
    // content hash cannot catch that, because a cached frame always hashes to the value
    // it recorded.
    std::uint64_t parameterHash{0U};
    int framesPerSecond{24};
    int startFrame{0};
    int endFrame{0};

    bool operator==(const SimulationCacheKey& other) const;
    bool operator!=(const SimulationCacheKey& other) const { return !(*this == other); }
    // Human readable difference list, used in diagnostics.
    std::string describeDifferences(const SimulationCacheKey& other) const;
};

struct SimulationCacheEntity {
    SimulationCacheEntity() = default;
    SimulationCacheEntity(SceneEntityId identity, SceneTransform pose, glm::vec3 color)
        : id(identity), transform(std::move(pose)), tint(color) {}
    SimulationCacheEntity(SceneEntityId identity, SceneTransform pose, glm::vec3 color,
                          std::optional<SceneLightComponent> component)
        : id(identity), transform(std::move(pose)), tint(color), light(std::move(component)), lightRecorded(true) {}
    SceneEntityId id{invalidSceneEntityId};
    SceneTransform transform;
    glm::vec3 tint{1.0f};
    std::optional<SceneLightComponent> light;
    bool lightRecorded{false}; // Missing legacy field preserves the input component.
};

struct SimulationCacheFrame {
    int frame{0};
    std::uint64_t contentHash{0U};
    std::string moduleState;
    std::vector<SimulationCacheEntity> entities;
};

struct SimulationCache {
    static constexpr int currentSchemaVersion = 1;

    int schemaVersion{currentSchemaVersion};
    SimulationCacheKey key;
    std::vector<SimulationCacheFrame> frames;

    const SimulationCacheFrame* find(int frame) const;
    bool covers(int frame) const;
};

enum class SimulationCacheStatus {
    Disabled = 0,
    // No cache file yet: normal for a first render.
    Missing,
    // A cache exists but was produced from different inputs; it must not be reused.
    Stale,
    Hit
};

const char* simulationCacheStatusName(SimulationCacheStatus status);

// Atomic write through `<path>.partial` + rename, matching the other runtimes.
bool saveSimulationCache(
    const std::filesystem::path& path,
    const SimulationCache& cache,
    std::string& error
);
bool loadSimulationCache(
    const std::filesystem::path& path,
    SimulationCache& cache,
    std::string& error
);

// Compares a loaded cache against the inputs of the run that wants to reuse it.
SimulationCacheStatus classifySimulationCache(
    const SimulationCache& cache,
    const SimulationCacheKey& requested,
    std::string& message
);
