#pragma once

#include "module/ModuleRuntime.h"

// Shared by CPU and Raster sidecars. Timings are diagnostics, never cache state.
template<class Writer>
void writeModuleRunReportJson(Writer& w, const ModuleRunReport& report) {
    w.StartObject();
    w.Key("id"); w.String(report.moduleId.c_str());
    w.Key("apiVersion"); w.Int(report.apiVersion);
    w.Key("buildId"); w.String(report.buildId.c_str());
    w.Key("seed"); w.Uint(report.seed);
    w.Key("lastFrame"); w.Int(report.lastFrame);
    w.Key("inputHash"); w.Uint64(report.inputContentHash);
    w.Key("contentHash"); w.Uint64(report.contentHash);
    w.Key("state"); w.String(report.moduleState.c_str());
    const auto& config = report.simulationConfiguration;
    if (!config.sdkVersion.empty()) {
        w.Key("physics"); w.StartObject();
        w.Key("sdk"); w.String(config.sdkVersion.c_str());
        w.Key("solver"); w.String(config.solver.c_str());
        w.Key("cacheSemantics"); w.String(config.cacheSemantics.c_str());
        w.Key("initialFrame"); w.Int(report.startFrame);
        w.Key("outputFps"); w.Int(report.framesPerSecond);
        w.Key("fixedStepSeconds"); w.Double(config.fixedStepSeconds);
        w.Key("substepsPerFrame"); w.Int(config.substepsPerFrame);
        w.Key("gravityY"); w.Double(config.gravityY);
        w.Key("positionIterations"); w.Int(config.positionIterations);
        w.Key("velocityIterations"); w.Int(config.velocityIterations);
        w.Key("bodyCount"); w.Uint64(config.bodyCount);
        w.Key("dynamicBodyCount"); w.Uint64(config.dynamicBodyCount);
        const auto& stats = report.simulation;
        w.Key("statistics"); w.StartObject();
        w.Key("measured"); w.Bool(stats.measured);
        if (stats.measured) {
            w.Key("totalSubsteps"); w.Uint64(stats.totalSubsteps);
            w.Key("lastAdvanceSubsteps"); w.Uint64(stats.lastAdvanceSubsteps);
            w.Key("lastAdvanceMilliseconds"); w.Double(stats.lastAdvanceMilliseconds);
        }
        w.EndObject(); w.EndObject();
    }
    w.EndObject();
}
