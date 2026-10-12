#pragma once

#include <cmath>
#include <string>
#include "runtime/RenderJob.h"
#include "runtime/Timeline.h"
#include "plugin/ShaderSceneHistory.h"

// Both preview replay and offline capture use Timeline's absolute time. Warmup
// precedes startFrame, including negative times when the range starts at zero.
struct ShaderSceneSequencePlan {
    int firstFrame{0};
    int lastFrame{0};
    Timeline timeline;

    float timeSeconds(int frame) const { return static_cast<float>(timeline.timeSeconds(frame)); }
    int sampleIndex(int frame) const { return frame - firstFrame; }
};

inline bool makeShaderSceneSequencePlan(const RenderJob& job,
    const iris::ShaderSceneHistoryPolicy& history, ShaderSceneSequencePlan& plan, std::string& error) {
    if (!validateRenderJob(job, error)) return false;
    if (!job.shaderSceneSequence) { error = "ShaderScene sequence requires explicit opt-in"; return false; }
    if (!iris::validateShaderSceneHistoryPolicy(history, error)) return false;
    ShaderSceneSequencePlan candidate;
    candidate.firstFrame = job.startFrame - job.shaderSceneWarmupFrames;
    candidate.lastFrame = job.endFrame;
    candidate.timeline.setFramesPerSecond(job.framesPerSecond);
    for (int frame = candidate.firstFrame + 1; frame <= candidate.lastFrame; ++frame) {
        const float delta = candidate.timeSeconds(frame) - candidate.timeSeconds(frame - 1);
        if (!(delta > 0.0f) || delta > history.maximumTimeStep) {
            error = "ShaderScene FPS/time range conflicts with history maximumTimeStep or float time precision";
            return false;
        }
    }
    plan = candidate;
    error.clear();
    return true;
}
