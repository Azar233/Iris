#pragma once

#include <stdexcept>
#include "optics/Atmosphere.h"
#include "optics/CloudFieldCpp.h"

// The one place a scene's cloud parameters become the shared density field's inputs.
//
// `MyRendererCloudParams` lives in `optics/CloudField.h`, which is GLSL as much as it is C++, and
// its members carry no defaults because GLSL 3.30 has no default member initialisers. Something has
// to fill it in, and the GPU side does that from its uniforms -- so the CPU side has exactly one
// place that does the same, here. Three call sites transcribing the same fourteen assignments is how
// a field ends up subtly different between the analytic layer, the CPU reference and the shader.
//
// Header-only and `inline` on purpose: `Atmosphere.cpp` and `CloudReference.cpp` sit in different
// CMake targets, and a shared translation unit for fourteen assignments would be a build dependency
// for nothing.
namespace cloud {

inline MyRendererCloudParams makeCloudParams(const atmosphere::AtmosphereParameters& parameters) {
    MyRendererCloudParams layer;
    layer.baseHeight = parameters.cloudBaseHeight;
    layer.topHeight = parameters.cloudTopHeight;
    layer.featureScale = parameters.cloudFeatureScale;
    layer.noisePeriod = std::clamp(
        static_cast<int>(std::lround(parameters.cloudNoisePeriod)), 1, 16);
    layer.offlineNoise = parameters.cloudOfflineNoise;
    if(layer.offlineNoise && layer.noisePeriod!=4)
        throw std::runtime_error("Offline cloud noise requires period 4");
    layer.windX = parameters.cloudWindOffsetX;
    layer.windZ = parameters.cloudWindOffsetZ;
    layer.coverage = parameters.cloudCoverage;
    layer.densityScale = parameters.cloudDensity;
    layer.weatherScale = parameters.cloudWeatherScale;
    layer.coverageVariation = parameters.cloudCoverageVariation;
    layer.cloudType = parameters.cloudType;
    layer.typeVariation = parameters.cloudTypeVariation;
    layer.heightVariation = parameters.cloudHeightVariation;
    layer.detailStrength = parameters.cloudDetailStrength;
    layer.detailEdge = parameters.cloudDetailEdge;
    return layer;
}

} // namespace cloud
