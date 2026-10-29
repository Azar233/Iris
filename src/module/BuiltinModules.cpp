#include "module/BuiltinModules.h"
#include "render/Camera.h"
#include "render/Renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <glm/common.hpp>
#include <glm/trigonometric.hpp>

void ISceneModule::applyPresentation(const SceneContext&,
    const CameraOrbitState& authoredCamera, const RendererSettings& authoredRenderer,
    CameraOrbitState& camera, RendererSettings& renderer) const {
    camera = authoredCamera;
    renderer = authoredRenderer;
}

namespace {

// The first statically linked module.
//
// It demonstrates the whole contract rather than a single call: parameters are
// declared once, the module captures a baseline in `initialize`, and every frame is
// computed as `baseline + f(frame)` instead of accumulating deltas. A scrub to frame
// k, a repeated run of frame k and a batch run of frame k therefore produce the same
// runtime scene, and `fixedUpdate` is idempotent for a given frame.
class TurntableModule final : public ISceneModule {
public:
    ModuleManifest manifest() const override {
        ModuleManifest result;
        result.id = BuiltinModules::turntableId;
        result.displayName = "Turntable";
        result.kind = ModuleKind::Scene;
        result.cmakeTarget = "MyRendererModules";
        result.sourceRoot = "src/module";
        result.apiVersion = moduleApiVersion;
        result.buildId = moduleBuildId();
        return result;
    }

    void registerParameters(ParameterRegistry& parameters) override {
        parameters.registerBool(
            "enabled", "Enabled", true,
            "Turns the deterministic turntable animation on or off; off restores the scene exactly."
        );
        parameters.registerFloat(
            "degreesPerFrame", "Degrees / frame", 15.0f, -90.0f, 90.0f,
            "Rotation applied per rendered frame; 15 deg over 24 frames is one turn."
        );
        parameters.registerEnum(
            "axis", "Rotation axis", {"X", "Y", "Z"}, 1,
            "World axis the turntable rotates around."
        );
        parameters.registerFloat(
            "carouselRadius", "Carousel radius", 0.0f, 0.0f, 20.0f,
            "Orbit radius around the world origin; 0 spins each object in place."
        );
        parameters.registerFloat(
            "bobHeight", "Vertical offset", 0.0f, -3.0f, 3.0f,
            "Constant vertical offset added to every animated object."
        );
        parameters.registerFloat(
            "phaseDegrees", "Phase", 0.0f, 0.0f, 360.0f,
            "Starting angle of the turntable."
        );
        parameters.registerColor(
            "tintColor", "Tint colour", glm::vec3(1.0f, 0.55f, 0.2f),
            "Colour blended into every animated object."
        );
        parameters.registerFloat(
            "tintStrength", "Tint strength", 0.0f, 0.0f, 1.0f,
            "Blend amount between the scene tint and the module tint colour."
        );
    }

    bool initialize(const SceneContext& context, std::string& error) override {
        (void)error;
        // Idempotent: the host calls reset() before rebuilding the runtime scene, so a
        // second initialize without reset must not capture already animated state.
        if (initialized_) return true;
        baseFrame_ = context.timeline().startFrame();
        baselines_.clear();
        for (const SceneEntity& entity : context.scene().entities()) {
            // Every entity that references a model participates. An entity with a
            // recorded resource but no uploaded geometry is included on purpose: its
            // transform must already be correct once the model arrives.
            if (entity.model == nullptr && entity.modelResource.empty()) continue;
            baselines_.push_back(Baseline{entity.id, entity.transform, entity.tint});
        }
        initialized_ = true;
        return true;
    }

    void reset() override {
        baselines_.clear();
        initialized_ = false;
        baseFrame_ = 0;
    }

    bool fixedUpdate(
        SceneContext& context,
        double fixedDeltaSeconds,
        std::string& error
    ) override {
        (void)fixedDeltaSeconds;
        if (!initialized_ && !initialize(context, error)) return false;

        const ParameterRegistry& parameters = context.parameters();
        const bool enabled = parameters.boolValue("enabled", true);
        const float degreesPerFrame = parameters.floatValue("degreesPerFrame", 15.0f);
        const ModuleParameterValue* axisParameter = parameters.value("axis");
        const int axis = std::clamp(
            axisParameter != nullptr ? axisParameter->integer : 1, 0, 2
        );
        const float carouselRadius = parameters.floatValue("carouselRadius", 0.0f);
        const float bobHeight = parameters.floatValue("bobHeight", 0.0f);
        const float phaseDegrees = parameters.floatValue("phaseDegrees", 0.0f);
        const glm::vec3 tintColor = parameters.colorValue(
            "tintColor", glm::vec3(1.0f, 0.55f, 0.2f)
        );
        const float tintStrength = parameters.floatValue("tintStrength", 0.0f);

        // Everything below is a pure function of the frame index, so the result never
        // depends on how that frame was reached. Disabling the module restores the
        // baseline exactly instead of leaving the last animated frame behind.
        const int step = context.timeline().frame() - baseFrame_;
        const float stepDegrees = enabled
            ? degreesPerFrame * static_cast<float>(step)
            : 0.0f;
        const float angle = glm::radians(phaseDegrees + stepDegrees);
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        glm::vec3 orbit(0.0f);
        switch (axis) {
            case 0: orbit = glm::vec3(0.0f, cosine, sine); break;
            case 1: orbit = glm::vec3(cosine, 0.0f, sine); break;
            default: orbit = glm::vec3(cosine, sine, 0.0f); break;
        }
        if (!enabled || carouselRadius == 0.0f) orbit = glm::vec3(0.0f);
        orbit *= carouselRadius;
        const glm::vec3 verticalOffset = enabled
            ? glm::vec3(0.0f, bobHeight, 0.0f)
            : glm::vec3(0.0f);
        const float rotationDelta = glm::degrees(glm::radians(stepDegrees));
        const float appliedTintStrength = enabled ? tintStrength : 0.0f;

        Scene& scene = context.scene();
        bool changed = false;
        for (const Baseline& baseline : baselines_) {
            SceneEntity* entity = scene.find(baseline.id);
            if (entity == nullptr) continue;
            SceneTransform next = baseline.transform;
            next.rotationDegrees[axis] =
                baseline.transform.rotationDegrees[axis] + rotationDelta;
            next.translation = baseline.transform.translation + orbit + verticalOffset;
            const glm::vec3 nextTint = glm::mix(
                baseline.tint, tintColor, appliedTintStrength
            );
            // "Dirty" means the runtime scene really differs from its input, so a frame
            // that reproduces the baseline does not force a re-render downstream.
            changed = changed
                || next.translation != entity->transform.translation
                || next.rotationDegrees != entity->transform.rotationDegrees
                || nextTint != entity->tint;
            entity->transform.translation = next.translation;
            entity->transform.rotationDegrees = next.rotationDegrees;
            entity->tint = nextTint;
            entity->motionHistoryValid = false;
        }
        if (changed) context.runtimeScene().markDirty();
        return true;
    }

    bool bake(const SceneContext& context, std::string& error) override {
        (void)error;
        context.log(
            ModuleLogSeverity::Info,
            "turntable bake: " + serializeState()
        );
        return true;
    }

    std::string serializeState() const override {
        std::uint64_t hash = 1469598103934665603ULL;
        for (const Baseline& baseline : baselines_) {
            const float values[7]{
                baseline.transform.translation.x,
                baseline.transform.translation.y,
                baseline.transform.translation.z,
                baseline.transform.rotationDegrees.x,
                baseline.transform.rotationDegrees.y,
                baseline.transform.rotationDegrees.z,
                static_cast<float>(baseline.id)
            };
            for (float value : values) {
                std::uint32_t bits = 0U;
                std::memcpy(&bits, &value, sizeof(bits));
                hash = (hash ^ static_cast<std::uint64_t>(bits)) * 1099511628211ULL;
            }
        }
        return "turntable|targets=" + std::to_string(baselines_.size())
            + "|baseFrame=" + std::to_string(baseFrame_)
            + "|baseline=" + std::to_string(hash);
    }

private:
    struct Baseline {
        SceneEntityId id{invalidSceneEntityId};
        SceneTransform transform;
        glm::vec3 tint{1.0f};
    };

    std::vector<Baseline> baselines_;
    bool initialized_{false};
    int baseFrame_{0};
};

} // namespace

ModuleRegistry createBuiltinModuleRegistry() {
    ModuleRegistry registry;
    std::string error;
    registry.add(
        TurntableModule{}.manifest(),
        []() -> std::unique_ptr<ISceneModule> {
            return std::make_unique<TurntableModule>();
        },
        error
    );
    const auto coastal = makeCoastalSequenceModule();
    registry.add(coastal->manifest(), []() { return makeCoastalSequenceModule(); }, error);
#if IRIS_ENABLE_PHYSX
    const auto physics = makeRigidBodyModule();
    registry.add(physics->manifest(), []() { return makeRigidBodyModule(); }, error);
#endif
    return registry;
}
