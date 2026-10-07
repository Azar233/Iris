#include "pathtracer/ProgressiveRenderer.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

#define STB_IMAGE_IMPLEMENTATION
#include "asset/StbImageCompat.h"

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

pathtracer::RenderImage render(
    const pathtracer::SceneSnapshot& scene,
    const pathtracer::RenderSettings& settings
) {
    pathtracer::ProgressiveRenderer renderer(scene, settings);
    while (renderer.renderPass()) {}
    return renderer.image();
}

void staleTaskCannotPublish() {
    const pathtracer::SceneSnapshot scene = pathtracer::makeDiffuseAcceptanceScene();
    pathtracer::RenderSettings longSettings{12U, 10U, 1000000U, 5U, 42U};
    longSettings.progressPublishMilliseconds = 0U;
    pathtracer::DenoiseSettings denoise;
    denoise.enabled = true;
    denoise.atrousIterations = 2U;
    pathtracer::RenderTask task;
    const std::uint64_t staleId = task.start(
        scene, longSettings, pathtracer::RenderOutput::Beauty, denoise);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (task.progressSnapshot()->staging.completedSamples == 0U
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    require(task.progressSnapshot()->staging.completedSamples > 0U,
            "Long task did not publish a staging image");

    pathtracer::RenderSettings replacementSettings{12U, 10U, 4U, 5U, 42U};
    const pathtracer::RenderImage expected = render(scene, replacementSettings);
    const pathtracer::DenoisedImage expectedDenoised = pathtracer::denoiseAovs(
        expected, denoise);
    const std::uint64_t replacementId = task.start(
        scene, replacementSettings, pathtracer::RenderOutput::Beauty, denoise);
    task.wait();
    const auto publication = task.progressSnapshot();
    require(replacementId > staleId, "Task IDs are not monotonic");
    require(publication->taskId == replacementId,
            "A stale task replaced the current publication");
    require(publication->status == pathtracer::RenderStatus::Completed,
            "Replacement task did not complete");
    require(publication->image.sum == expected.sum,
            "Replacement task accumulated stale samples");
    require(publication->denoised.beauty == expectedDenoised.beauty,
            "Replacement task published stale denoiser history");
}

void guiAndCliPixelsMatch() {
    const pathtracer::SceneSnapshot scene = pathtracer::makeDiffuseAcceptanceScene();
    pathtracer::RenderSettings settings{9U, 7U, 4U, 5U, 20260917U};
    const pathtracer::RenderImage expected = render(scene, settings);

    pathtracer::RenderTask task;
    const std::uint64_t taskId = task.start(
        scene, settings, pathtracer::RenderOutput::Beauty
    );
    task.wait();
    const auto publication = task.progressSnapshot();
    require(publication->taskId == taskId
                && publication->image.sum == expected.sum,
            "GUI task and synchronous CLI render differ");
    require(publication->staging.rgba
                == pathtracer::makeDisplayRgba8BottomUp(
                    expected, pathtracer::RenderOutput::Beauty
                ),
            "Worker staging and shared display conversion differ");

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "myrenderer-cpu-preview-test";
    const std::filesystem::path stem = directory / "parity";
    pathtracer::writeReferenceImage(expected, stem);
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* png = stbi_load(
        (stem.string() + ".png").c_str(), &width, &height, &channels, 3
    );
    require(png != nullptr && width == static_cast<int>(settings.width)
                && height == static_cast<int>(settings.height),
            "CLI PNG could not be decoded");
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t filePixel = static_cast<std::size_t>(y * width + x);
            const std::size_t stagingPixel = static_cast<std::size_t>(
                (height - 1 - y) * width + x
            );
            for (int channel = 0; channel < 3; ++channel) {
                require(
                    png[filePixel * 3U + static_cast<std::size_t>(channel)]
                        == publication->staging.rgba[
                            stagingPixel * 4U + static_cast<std::size_t>(channel)
                        ],
                    "GUI staging and CLI PNG are not byte-identical"
                );
            }
        }
    }
    stbi_image_free(png);
    std::filesystem::remove(stem.string() + ".png");
    std::filesystem::remove(stem.string() + ".hdr");
    std::filesystem::remove(directory);
}

void denoisedGuiAndCliPixelsMatch() {
    const pathtracer::SceneSnapshot scene = pathtracer::makePbrAcceptanceScene();
    pathtracer::RenderSettings settings{13U, 11U, 4U, 5U, 20260917U};
    const pathtracer::RenderImage raw = render(scene, settings);
    pathtracer::DenoiseSettings denoise;
    denoise.enabled = true;
    denoise.atrousIterations = 2U;
    const pathtracer::DenoisedImage expected = pathtracer::denoiseAovs(raw, denoise);

    pathtracer::RenderTask task;
    const std::uint64_t taskId = task.start(
        scene, settings, pathtracer::RenderOutput::Beauty, denoise);
    task.wait();
    const auto publication = task.progressSnapshot();
    require(publication->taskId == taskId && publication->image.sum == raw.sum,
            "Denoised GUI task changed unbiased accumulation");
    require(publication->denoised.beauty == expected.beauty,
            "GUI and CLI AOV denoising differ");
    require(publication->staging.rgba == pathtracer::makeDisplayRgba8BottomUp(
                expected, pathtracer::RenderOutput::Beauty),
            "Denoised GUI staging and CLI conversion differ");
}

} // namespace

int main() {
    try {
        staleTaskCannotPublish();
        guiAndCliPixelsMatch();
        denoisedGuiAndCliPixelsMatch();
        std::cout << "CPU progressive preview tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
