#include <exception>
#include <filesystem>
#include <iostream>

#include "app/Application.h"
#include "runtime/RenderJob.h"

int main(int argc, char** argv) {
    try {
        if (argc >= 2 && std::string(argv[1]) == "raster-sequence") {
            if (argc != 3 && (argc != 5 || std::string(argv[3]) != "--output")) {
                std::cerr << "Usage: Iris raster-sequence <job.renderjob> "
                             "[--output <frame-pattern>]\n";
                return 64;
            }
            RenderJob job;
            std::string error;
            if (!loadRenderJob(argv[2], job, error)) {
                std::cerr << "Render Job invalid: " << error << '\n';
                return 65;
            }
            if (argc == 5 && !applyRenderJobOutputOverride(job,
                    std::filesystem::absolute(argv[4]).lexically_normal(), error)) {
                std::cerr << "Render Job output override invalid: " << error << '\n';
                return 65;
            }
            Application application;
            return application.runRasterSequence(job);
        }
        const std::filesystem::path initialModel = argc > 1
            ? std::filesystem::path(argv[1])
            : std::filesystem::path{};
        Application application;
        return application.run(initialModel);
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        return 1;
    }
}
