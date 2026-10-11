#include "EnscapeCubeRenderer.h"
#include "plugin/ShaderSceneRenderer.h"
#include "scene/SceneDocument.h"
#include "render/Shader.h"
#include "render/RenderTarget.h"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::vector<unsigned char> pixels(const RenderTarget& target) {
    std::vector<unsigned char> bytes(static_cast<std::size_t>(target.width() * target.height() * 4));
    glBindTexture(GL_TEXTURE_2D, target.colorTexture()); glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, bytes.data());
    check(glGetError() == GL_NO_ERROR, "Enscape comparison GL error"); return bytes;
}
double compare(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b, const char* label, bool sequence = false) {
    check(a.size() == b.size(), "Comparison extent differs"); double sum = 0; std::size_t changed = 0;
    for (std::size_t pixel = 0; pixel < a.size(); pixel += 4) {
        int maximum = 0;
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const int difference = std::abs(static_cast<int>(a[pixel + channel]) - b[pixel + channel]);
            sum += difference; maximum = std::max(maximum, difference);
        }
        if (maximum > 2) ++changed;
    }
    const double mae = sum / (static_cast<double>(a.size() / 4) * 3 * 255);
    const double fraction = static_cast<double>(changed) / (a.size() / 4);
    std::cout << label << " MAE=" << mae << " changed>2/255=" << fraction * 100 << "%\n";
    check(mae <= (sequence ? 0.001 : 0.0005) && fraction <= (sequence ? 0.005 : 0.002), "Old/package image gate failed"); return mae;
}
double percentile(std::vector<double> samples, double quantile) {
    std::sort(samples.begin(), samples.end()); return samples[static_cast<std::size_t>(std::ceil(quantile * samples.size())) - 1];
}
struct Cost { std::vector<double> cpu, gpu; };
}
int main(int argc, char** argv) {
    if (argc != 3 || !glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3); glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(64, 64, "Enscape migration acceptance", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window); if (!gladLoadGL(glfwGetProcAddress)) return 1;
    int result = 0;
    try {
        const auto shaders = std::filesystem::u8path(argv[1]); const auto root = shaders.parent_path();
        const auto output = std::filesystem::u8path(argv[2]); std::filesystem::create_directories(output);
        GLuint vao = 0; glGenVertexArrays(1, &vao);
        {
            EnscapeCubeRenderer legacy(shaders); iris::ShaderSceneRenderer migrated(shaders);
            RenderTarget oldTarget, newTarget; oldTarget.resize(320, 180, 1); newTarget.resize(320, 180, 1);
            Camera camera; RendererSettings settings;
            settings.enscapeCubeShaderEnabled = true;
            settings.enscapeShaderSceneResource = (root / "assets/shader-scenes/enscape/enscape.shaderscene").generic_u8string();
            const auto draw = [&](iris::RenderPlugin& renderer, RenderTarget& target, float time) {
                renderer.renderFrame({target, camera, settings, target.width(), target.height(), time, vao}); return pixels(target);
            };
            for (const auto* scene : {"01_ocean_clouds_hero.myscene", "03_enscape_ocean_study.myscene"}) {
                SceneDocument document; std::string error;
                check(loadSceneDocument(root / "assets/scenes" / scene, document, error), error.c_str());
                settings = document.renderer; settings.enscapeCubeShaderEnabled = true;
                settings.enscapeShaderSceneResource = (root / "assets/shader-scenes/enscape/enscape.shaderscene").generic_u8string();
                camera.setOrbitState(document.camera); legacy.prepareResources(settings, 320, 180); migrated.prepareResources(settings, 320, 180);
                check(legacy.estimatedBytes() == migrated.estimatedBytes(), "Migration increased private texture budget");
                std::vector<unsigned char> withoutReduction;
                for (const bool noiseReduction : {false, true}) {
                    settings.enscapeCube.noiseReduction = noiseReduction; legacy.invalidateHistory(); migrated.invalidateHistory();
                    const auto old = draw(legacy, oldTarget, 1.25f), current = draw(migrated, newTarget, 1.25f);
                    compare(old, current, scene);
                    if (!noiseReduction) withoutReduction = current;
                    else check(withoutReduction != current, "noiseReduction has no visible contribution");
                    check(current == draw(migrated, newTarget, 1.25f), "Paused package frame accumulated history");
                    const auto name = std::filesystem::path(scene).stem().string() + (noiseReduction ? "-stable" : "-legacy");
                    check(oldTarget.savePng(output / (name + "-old.png"), error), error.c_str());
                    check(newTarget.savePng(output / (name + "-package.png"), error), error.c_str());
                    legacy.invalidateHistory(); migrated.invalidateHistory();
                    std::vector<std::vector<unsigned char>> sequence;
                    for (int frame = 0; frame < 24; ++frame) {
                        const float time = static_cast<float>(frame) / 24;
                        const auto reference = draw(legacy, oldTarget, time); auto image = draw(migrated, newTarget, time);
                        compare(reference, image, "sequence", true); sequence.push_back(std::move(image));
                    }
                    migrated.invalidateHistory();
                    for (int frame = 0; frame < 24; ++frame)
                        check(sequence[static_cast<std::size_t>(frame)] == draw(migrated, newTarget, static_cast<float>(frame) / 24), "Migration sequence replay drifted");
                }
                settings.enscapeCube.waveHeight = 0.9f; settings.enscapeCube.exposure = 1.3f;
                compare(draw(legacy, oldTarget, 1.0f), draw(migrated, newTarget, 1.0f), "parameter reset");
                camera.moveLocal(0.3f, 0.1f); compare(draw(legacy, oldTarget, 1.05f), draw(migrated, newTarget, 1.05f), "camera reset");
                camera.setOrbitPose(glm::vec3(0, 0, -2), 45, -25, 3.2f, 65);
                check(camera.position().y < -1.0f, "Underwater test camera is not submerged");
                compare(draw(legacy, oldTarget, 1.1f), draw(migrated, newTarget, 1.1f), "underwater camera");
                const auto retained = draw(migrated, newTarget, 1.1f);
                settings.enscapeCube.exposure = 100; bool rejected = false;
                try { migrated.prepareResources(settings, 320, 180); } catch (const std::exception&) { rejected = true; }
                settings.enscapeCube.exposure = 1.3f;
                check(rejected && retained == draw(migrated, newTarget, 1.1f), "Bad source parameter lost live output");
            }
            // Paired timestamp queries do not nest the legacy owner's GL_TIME_ELAPSED query.
            SceneDocument benchmark; std::string error;
            check(loadSceneDocument(root / "assets/scenes/01_ocean_clouds_hero.myscene", benchmark, error), error.c_str());
            settings = benchmark.renderer; settings.enscapeShaderSceneResource = (root / "assets/shader-scenes/enscape/enscape.shaderscene").generic_u8string();
            camera.setOrbitState(benchmark.camera); oldTarget.resize(640, 360, 1); newTarget.resize(640, 360, 1);
            legacy.prepareResources(settings, 640, 360); migrated.prepareResources(settings, 640, 360);
            GLuint queries[2]{}; glGenQueries(2, queries); Cost oldCost, newCost;
            const auto measured = [&](iris::RenderPlugin& renderer, RenderTarget& target, Cost& cost, float time) {
                glQueryCounter(queries[0], GL_TIMESTAMP); const auto start = std::chrono::steady_clock::now();
                renderer.renderFrame({target, camera, settings, 640, 360, time, vao});
                const auto end = std::chrono::steady_clock::now(); glQueryCounter(queries[1], GL_TIMESTAMP);
                GLuint64 first = 0, last = 0; glGetQueryObjectui64v(queries[0], GL_QUERY_RESULT, &first); glGetQueryObjectui64v(queries[1], GL_QUERY_RESULT, &last);
                cost.cpu.push_back(std::chrono::duration<double, std::milli>(end - start).count()); cost.gpu.push_back(static_cast<double>(last - first) / 1e6);
            };
            for (int frame = 0; frame < 96; ++frame) {
                const float time = 2 + static_cast<float>(frame) / 30;
                if (frame % 2) { measured(migrated, newTarget, newCost, time); measured(legacy, oldTarget, oldCost, time); }
                else { measured(legacy, oldTarget, oldCost, time); measured(migrated, newTarget, newCost, time); }
                if (frame == 23) { oldCost = {}; newCost = {}; }
            }
            glDeleteQueries(2, queries); check(glGetError() == GL_NO_ERROR, "Benchmark GL error");
            check(migrated.frameInfo().gpuTimeValid && migrated.frameInfo().gpuTimeUpdated && migrated.frameInfo().gpuMilliseconds > 0,
                "ShaderScene owner did not publish asynchronous GPU timing");
            const double oldGpu95 = percentile(oldCost.gpu, 0.95), newGpu95 = percentile(newCost.gpu, 0.95);
            const double oldCpu95 = percentile(oldCost.cpu, 0.95), newCpu95 = percentile(newCost.cpu, 0.95);
            std::ofstream report(output / "cost.json");
            report << "{\"frames\":72,\"width\":640,\"height\":360,\"legacyBytes\":" << legacy.estimatedBytes()
                << ",\"packageBytes\":" << migrated.estimatedBytes() << ",\"legacyGpuP50\":" << percentile(oldCost.gpu, 0.5)
                << ",\"packageGpuP50\":" << percentile(newCost.gpu, 0.5) << ",\"legacyGpuP95\":" << oldGpu95 << ",\"packageGpuP95\":" << newGpu95
                << ",\"legacyCpuP50\":" << percentile(oldCost.cpu, 0.5) << ",\"packageCpuP50\":" << percentile(newCost.cpu, 0.5)
                << ",\"legacyCpuP95\":" << oldCpu95 << ",\"packageCpuP95\":" << newCpu95 << "}\n";
            std::cout << "GPU P95 old/package " << oldGpu95 << '/' << newGpu95 << " ms; CPU " << oldCpu95 << '/' << newCpu95 << " ms\n";
            check(newGpu95 <= oldGpu95 * 1.15 + 0.1 && newCpu95 <= oldCpu95 + 0.30, "Migration cost gate failed");
            check(legacy.estimatedBytes() == migrated.estimatedBytes(), "Benchmark memory gate failed");
        }
        glDeleteVertexArrays(1, &vao); std::cout << "Enscape package / 01+03 / noiseReduction / sequence / parameters+camera / atomic rejection / cost PASS\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    glfwDestroyWindow(window); glfwTerminate(); return result;
}
