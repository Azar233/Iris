#include "module/ModuleRuntime.h"
#include "render/Camera.h"
#include "render/Renderer.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

// Translates one persisted value into the shape the registered descriptor expects.
bool coerceValue(
    const ModuleParameterDescriptor& descriptor,
    const ModuleParameterValue& value,
    ModuleParameterValue& coerced,
    std::string& error
) {
    coerced = value;
    coerced.type = descriptor.type;
    // Scene overrides preserve their declared type; Job strings remain neutral Asset values.
    if (value.type == descriptor.type && descriptor.type != ModuleParameterType::Enum) return true;
    switch (descriptor.type) {
        case ModuleParameterType::Bool:
            error = "parameter '" + descriptor.id + "' expects a bool, got "
                + moduleParameterTypeName(value.type);
            return false;
        case ModuleParameterType::Int:
            if (value.type == ModuleParameterType::Float) {
                if (!std::isfinite(value.number)) {
                    error = "parameter '" + descriptor.id + "' must be a finite number";
                    return false;
                }
                coerced.integer = static_cast<int>(std::lround(std::clamp(
                    static_cast<double>(value.number), descriptor.minimum, descriptor.maximum)));
                return true;
            }
            error = "parameter '" + descriptor.id + "' expects a number, got "
                + moduleParameterTypeName(value.type);
            return false;
        case ModuleParameterType::Float:
            if (value.type == ModuleParameterType::Int) {
                coerced.number = static_cast<float>(value.integer);
                return true;
            }
            if (value.type != ModuleParameterType::Float) {
                error = "parameter '" + descriptor.id + "' expects a number, got "
                    + moduleParameterTypeName(value.type);
                return false;
            }
            coerced.number = value.number;
            return true;
        case ModuleParameterType::Color:
            if (value.type != ModuleParameterType::Color) {
                error = "parameter '" + descriptor.id + "' expects a colour, got "
                    + moduleParameterTypeName(value.type);
                return false;
            }
            coerced.color = value.color;
            return true;
        case ModuleParameterType::Enum:
            if (value.type != ModuleParameterType::Asset && value.type != ModuleParameterType::Enum) {
                error = "parameter '" + descriptor.id + "' expects an enum label, got "
                    + moduleParameterTypeName(value.type);
                return false;
            }
            coerced.text = value.text;
            {
                const auto label = std::find(descriptor.enumLabels.begin(), descriptor.enumLabels.end(), value.text);
                if (label == descriptor.enumLabels.end()) {
                    error = "parameter '" + descriptor.id + "' has unknown enum label '" + value.text + "'";
                    return false;
                }
                coerced.integer = static_cast<int>(label - descriptor.enumLabels.begin());
            }
            return true;
        case ModuleParameterType::Asset:
            if (value.type != ModuleParameterType::Asset) {
                error = "parameter '" + descriptor.id + "' expects an asset path, got "
                    + moduleParameterTypeName(value.type);
                return false;
            }
            coerced.text = value.text;
            return true;
    }
    error = "parameter '" + descriptor.id + "' has an unsupported type";
    return false;
}

} // namespace

const char* moduleRunStatusName(ModuleRunStatus status) {
    switch (status) {
        case ModuleRunStatus::Idle: return "Idle";
        case ModuleRunStatus::Ready: return "Ready";
        case ModuleRunStatus::Cancelled: return "Cancelled";
        case ModuleRunStatus::Failed: return "Failed";
    }
    return "Unknown";
}

bool applyParameterOverrides(
    ParameterRegistry& parameters,
    const std::vector<ModuleParameterOverride>& overrides,
    std::string& error
) {
    if (overrides.empty()) return true;
    // Coerce every value against its descriptor first, then let the registry apply the
    // whole set transactionally.
    std::vector<ModuleParameterOverride> coerced;
    coerced.reserve(overrides.size());
    for (const ModuleParameterOverride& entry : overrides) {
        const ModuleParameterDescriptor* descriptor = parameters.descriptor(entry.id);
        if (descriptor == nullptr) {
            error = "unknown module parameter '" + entry.id + "'";
            return false;
        }
        ModuleParameterValue value;
        if (!coerceValue(*descriptor, entry.value, value, error)) return false;
        coerced.push_back(ModuleParameterOverride{entry.id, value});
    }
    return parameters.applyOverrides(coerced, error);
}

ModuleRuntime::ModuleRuntime(const ModuleRegistry& registry)
    : registry_(&registry),
      context_(runtimeScene_, timeline_, parameters_, 0U) {}

bool ModuleRuntime::configure(
    const std::string& moduleId,
    const std::vector<ModuleParameterOverride>& overrides,
    std::uint32_t seed,
    std::string& error
) {
    clear();
    if (moduleId.empty()) {
        error.clear();
        return true;
    }
    if (registry_ == nullptr) {
        error = "No module registry is available";
        return false;
    }
    module_ = registry_->create(moduleId, error);
    if (module_ == nullptr) return false;

    parameters_.clear();
    module_->registerParameters(parameters_);
    if (!applyParameterOverrides(parameters_, overrides, error)) {
        module_.reset();
        parameters_.clear();
        return false;
    }

    const ModuleManifest manifest = module_->manifest();
    report_ = ModuleRunReport{};
    report_.moduleId = manifest.id;
    report_.moduleDisplayName = manifest.displayName;
    report_.apiVersion = manifest.apiVersion;
    report_.buildId = manifest.buildId;
    report_.seed = seed;
    context_ = SceneContext(runtimeScene_, timeline_, parameters_, seed);
    error.clear();
    return true;
}

void ModuleRuntime::clear() {
    module_.reset();
    parameters_.clear();
    runtimeScene_ = RuntimeScene{};
    timeline_ = Timeline{};
    report_ = ModuleRunReport{};
    context_ = SceneContext(runtimeScene_, timeline_, parameters_, 0U);
}

void ModuleRuntime::configureTimeline(int startFrame, int endFrame, int framesPerSecond) {
    timeline_.setLoop(false);
    timeline_.setFramesPerSecond(framesPerSecond);
    timeline_.setFrameRange(startFrame, endFrame);
    timeline_.setFrame(timeline_.startFrame());
}

bool ModuleRuntime::reset(
    const Scene& editScene,
    int startFrame,
    int endFrame,
    int framesPerSecond,
    std::string& error
) {
    if (module_ == nullptr) {
        error = "No module is configured";
        return false;
    }
    configureTimeline(startFrame, endFrame, framesPerSecond);
    runtimeScene_.resetFrom(editScene);
    report_.startFrame = timeline_.startFrame();
    report_.endFrame = timeline_.endFrame();
    report_.framesPerSecond = timeline_.framesPerSecond();
    report_.inputContentHash = runtimeScene_.contentHash();
    report_.lastFrame = timeline_.frame();
    report_.contentHash = report_.inputContentHash;
    report_.moduleState.clear();
    report_.error.clear();
    report_.status = ModuleRunStatus::Idle;
    context_.clearLog();

    if (!module_->initialize(context_, error)) {
        report_.status = ModuleRunStatus::Failed;
        report_.error = error;
        return false;
    }
    // The start frame is evaluated as well, so a scrub to it is a real state and not
    // "the module has not run yet".
    if (!module_->fixedUpdate(context_, timeline_.fixedDeltaSeconds(), error)) {
        report_.status = ModuleRunStatus::Failed;
        report_.error = error;
        return false;
    }
    runtimeScene_.scene().updateWorldTransforms();
    report_.status = ModuleRunStatus::Ready;
    report_.contentHash = runtimeScene_.contentHash();
    report_.moduleState = module_->serializeState();
    return !failIfModuleLoggedError(error);
}

bool ModuleRuntime::runToFrame(int frame, std::string& error) {
    if (module_ == nullptr) {
        error = "No module is configured";
        return false;
    }
    if (report_.status == ModuleRunStatus::Failed) {
        error = report_.error.empty() ? "Module run already failed" : report_.error;
        return false;
    }
    if (frame < timeline_.frame() || frame > timeline_.endFrame()) {
        error = "Module runtime steps forward only; reset before an earlier frame";
        return false;
    }
    while (timeline_.frame() < frame) {
        if (context_.cancellationRequested()) {
            report_.status = ModuleRunStatus::Cancelled;
            error = "Module run cancelled";
            report_.error = error;
            return false;
        }
        timeline_.advance();
        if (!module_->fixedUpdate(context_, timeline_.fixedDeltaSeconds(), error)) {
            // A failing module must not take the editor scene or the render context
            // down with it: the runtime reports and stops publishing.
            report_.status = ModuleRunStatus::Failed;
            report_.error = error.empty() ? "Module fixedUpdate failed" : error;
            return false;
        }
        runtimeScene_.scene().updateWorldTransforms();
        report_.lastFrame = timeline_.frame();
    }
    report_.status = ModuleRunStatus::Ready;
    report_.contentHash = runtimeScene_.contentHash();
    report_.moduleState = module_->serializeState();
    return !failIfModuleLoggedError(error);
}

void ModuleRuntime::applyPresentation(const CameraOrbitState& authoredCamera,
    const RendererSettings& authoredRenderer, CameraOrbitState& camera,
    RendererSettings& renderer) const {
    camera = authoredCamera;
    renderer = authoredRenderer;
    if (module_ != nullptr && report_.status == ModuleRunStatus::Ready) {
        module_->applyPresentation(context_, authoredCamera, authoredRenderer, camera, renderer);
    }
}

bool ModuleRuntime::failIfModuleLoggedError(std::string& error) {
    if (!context_.hasErrors()) {
        error.clear();
        return false;
    }
    for (const ModuleLogEntry& entry : context_.logEntries()) {
        if (entry.severity != ModuleLogSeverity::Error) continue;
        error = entry.message;
        break;
    }
    if (error.empty()) error = "Module logged an error";
    report_.status = ModuleRunStatus::Failed;
    report_.error = error;
    return true;
}

bool ModuleRuntime::bake(std::string& error) {
    if (module_ == nullptr) {
        error = "No module is configured";
        return false;
    }
    if (!module_->bake(context_, error)) {
        report_.status = ModuleRunStatus::Failed;
        report_.error = error.empty() ? "Module bake failed" : error;
        return false;
    }
    report_.moduleState = module_->serializeState();
    error.clear();
    return true;
}

SimulationCacheKey ModuleRuntime::cacheKey(std::uint64_t sceneContentHash) const {
    SimulationCacheKey key;
    key.sceneContentHash = sceneContentHash;
    key.moduleId = report_.moduleId;
    key.moduleApiVersion = report_.apiVersion;
    key.buildId = report_.buildId;
    key.seed = report_.seed;
    key.parameterHash = parameters_.fingerprint();
    key.framesPerSecond = timeline_.framesPerSecond();
    key.startFrame = timeline_.startFrame();
    key.endFrame = timeline_.endFrame();
    return key;
}

SimulationCacheKey ModuleRuntime::cacheKey() const {
    return cacheKey(report_.inputContentHash);
}

bool ModuleRuntime::bakeSimulation(
    const Scene& editScene,
    int startFrame,
    int endFrame,
    int framesPerSecond,
    SimulationCache& cache,
    std::string& error
) {
    if (module_ == nullptr) {
        error = "No module is configured";
        return false;
    }
    if (!reset(editScene, startFrame, endFrame, framesPerSecond, error)) return false;
    cache = SimulationCache{};
    cache.key = cacheKey();
    for (int frame = startFrame; frame <= endFrame; ++frame) {
        if (!runToFrame(frame, error)) return false;
        SimulationCacheFrame record;
        record.frame = frame;
        record.contentHash = report_.contentHash;
        record.moduleState = report_.moduleState;
        // Every entity is recorded, not only the ones the module moved: reconstructing a
        // frame must not depend on guessing which fields a module may touch.
        for (const SceneEntity& entity : runtimeScene_.scene().entities()) {
            record.entities.push_back(SimulationCacheEntity{
                entity.id, entity.transform, entity.tint, entity.light
            });
        }
        cache.frames.push_back(std::move(record));
    }
    error.clear();
    return true;
}

bool ModuleRuntime::applyCachedFrame(const SimulationCacheFrame& frame, std::string& error) {
    if (module_ == nullptr) {
        error = "No module is configured";
        return false;
    }
    Scene& scene = runtimeScene_.scene();
    for (const SimulationCacheEntity& cached : frame.entities) {
        SceneEntity* entity = scene.find(cached.id);
        if (entity == nullptr) {
            error = "Cached entity " + std::to_string(cached.id)
                + " does not exist in the runtime scene";
            return false;
        }
        entity->transform = cached.transform;
        entity->tint = cached.tint;
        if (cached.lightRecorded) entity->light = cached.light;
        entity->motionHistoryValid = false;
    }
    scene.updateWorldTransforms();
    const std::uint64_t actual = runtimeScene_.contentHash();
    if (actual != frame.contentHash) {
        error = "Cached frame " + std::to_string(frame.frame)
            + " does not reproduce its recorded content hash";
        return false;
    }
    report_.lastFrame = frame.frame;
    timeline_.setFrame(frame.frame);
    report_.contentHash = actual;
    report_.moduleState = frame.moduleState;
    report_.status = ModuleRunStatus::Ready;
    error.clear();
    return true;
}
