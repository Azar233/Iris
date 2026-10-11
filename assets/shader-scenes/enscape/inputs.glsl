uniform vec2 irisResolution;
uniform float irisTime;
uniform bool irisHistoryValid;
uniform vec3 irisCameraPosition;
uniform vec3 irisCameraForward;
uniform vec3 irisCameraRight;
uniform vec3 irisCameraUp;
uniform float irisCameraTanHalfFov;
uniform float irisCameraNear;
uniform bool cubeEnabled;
uniform bool noiseReduction;
uniform float waveHeight;
uniform float waveFrequency;
uniform float waveChoppiness;
uniform float waveSpeed;
uniform float cloudCoverage;
uniform float reflectionStrength;
uniform float underwaterClarity;
uniform float sunAzimuthDegrees;
uniform float sunElevationDegrees;
uniform float bloomStrength;
uniform float exposure;
#define iResolution vec3(irisResolution, 1.0)
#define iTime irisTime
#define uCameraPosition irisCameraPosition
#define uCameraForward irisCameraForward
#define uCameraRight irisCameraRight
#define uCameraUp irisCameraUp
#define uCameraTanHalfFov irisCameraTanHalfFov
#define uCameraNear irisCameraNear
#define uCubeEnabled cubeEnabled
#define uNoiseReduction noiseReduction
#define uWaveHeight waveHeight
#define uWaveFrequency waveFrequency
#define uWaveChoppiness waveChoppiness
#define uWaveSpeed waveSpeed
#define uCloudCoverage cloudCoverage
#define uReflectionStrength reflectionStrength
#define uUnderwaterClarity underwaterClarity
#define uBloomStrength bloomStrength
#define uExposure exposure
vec3 packageSunDirection() {
    const float degreesToRadians = 0.017453292519943295;
    float azimuth = sunAzimuthDegrees * degreesToRadians;
    float elevation = sunElevationDegrees * degreesToRadians;
    return vec3(cos(elevation) * sin(azimuth), sin(elevation), cos(elevation) * cos(azimuth));
}
#define uSunDirection packageSunDirection()
