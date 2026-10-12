#include <exception>
#include <filesystem>
#include <iostream>
#include <vector>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#include "app/Application.h"
#include "runtime/RenderJob.h"

int main(int argc, char** argv) {
    try {
#ifdef _WIN32
        int wideCount = 0;
        auto wideArgs = CommandLineToArgvW(GetCommandLineW(), &wideCount);
        if (!wideArgs) throw std::runtime_error("Cannot read command line");
        std::vector<std::string> arguments;
        for (int i = 0; i < wideCount; ++i) arguments.push_back(std::filesystem::path(wideArgs[i]).u8string());
        LocalFree(wideArgs);
        std::vector<char*> pointers;
        for (auto& argument : arguments) pointers.push_back(argument.data());
        argc = wideCount; argv = pointers.data();
#endif
        if (argc >= 2 && std::string(argv[1]) == "raster-sequence") {
            if (argc < 3 || argc % 2 == 0) {
                std::cerr << "Usage: Iris raster-sequence <job.renderjob> "
                             "[--output <frame-pattern>]\n";
                return 64;
            }
            RenderJob job;
            std::string error;
            if (!loadRenderJob(std::filesystem::u8path(argv[2]), job, error)) {
                std::cerr << "Render Job invalid: " << error << '\n';
                return 65;
            }
            std::string cancelEvent;
            std::vector<std::string> seen;
            for (int i = 3; i < argc; i += 2) {
                const std::string option = argv[i];
                if (std::find(seen.begin(), seen.end(), option) != seen.end()) return 64;
                seen.push_back(option);
                if (option == "--output") {
                    if (!applyRenderJobOutputOverride(job, std::filesystem::absolute(std::filesystem::u8path(argv[i+1])).lexically_normal(), error)) {
                        std::cerr << "Render Job output override invalid: " << error << '\n'; return 65;
                    }
                } else if (option == "--cancel-event") cancelEvent = argv[i+1];
                else if (option == "--job-fingerprint") {
                    if (job.loadedSourceFingerprint != argv[i+1]) { std::cerr << "Queued Render Job changed since parsing\n"; return 65; }
                } else return 64;
            }
            Application application(true);
            return application.runRasterSequence(job, cancelEvent);
        }
        const std::filesystem::path initialModel = argc > 1
            ? std::filesystem::u8path(argv[1])
            : std::filesystem::path{};
        Application application;
        return application.run(initialModel);
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        return 1;
    }
}
