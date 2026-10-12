#pragma once
#include "runtime/BatchRuntime.h"
#include "asset/InputManifest.h"
#include <atomic>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

// Queue workers never borrow the editor's GL objects. A child owns its context,
// sees the exact queued job identity, and acknowledges cancellation at a frame
// boundary. Handles close even if polling throws; cancellation is never a kill.
inline BatchSequenceResult runRasterSequenceProcess(const RenderJob& job,
    const CancellationToken* cancellation, const BatchSequenceProgressCallback& progress) {
    BatchSequenceResult result;
    result.status = BatchFrameStatus::Failed;
    result.totalFrames = job.endFrame - job.startFrame + 1;
    try {
        if (job.sourcePath.empty() || capture::snapshot(job.sourcePath).fingerprint != job.loadedSourceFingerprint)
            throw std::runtime_error("Queued Render Job changed since parsing");
        if (cancellation && cancellation->isCancellationRequested()) {
            result.status = BatchFrameStatus::Cancelled; return result;
        }
        for (int frame = job.startFrame; frame <= job.endFrame; ++frame) {
            if (std::filesystem::exists(renderJobFramePath(job, frame, ".png"))
                || std::filesystem::exists(renderJobFramePath(job, frame, "-report.json")))
                throw std::runtime_error("Raster output already exists");
        }
#ifdef _WIN32
        struct Handle {
            HANDLE value{nullptr};
            ~Handle() { if (value) CloseHandle(value); }
        };
        static std::atomic<unsigned int> serial{0};
        const auto eventName = L"Local\\IrisRasterCancel-" + std::to_wstring(GetCurrentProcessId())
            + L"-" + std::to_wstring(++serial);
        Handle event{CreateEventW(nullptr, TRUE, FALSE, eventName.c_str())};
        if (!event.value || GetLastError() == ERROR_ALREADY_EXISTS)
            throw std::runtime_error("Cannot create unique Raster cancellation event");
        wchar_t executable[32768]{};
        const auto length = GetModuleFileNameW(nullptr, executable, 32768);
        if (!length || length >= 32768) throw std::runtime_error("Cannot locate Iris executable");
        // Filesystem paths cannot contain quotes; no shell expansion is involved.
        auto command = std::wstring(L"\"") + executable + L"\" raster-sequence \"" + job.sourcePath.wstring()
            + L"\" --cancel-event " + eventName + L" --job-fingerprint "
            + std::wstring(job.loadedSourceFingerprint.begin(), job.loadedSourceFingerprint.end());
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        if (!CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info))
            throw std::runtime_error("Cannot launch Raster executor (Win32 " + std::to_string(GetLastError()) + ")");
        Handle process{info.hProcess}, thread{info.hThread};
        struct ChildGuard {
            HANDLE process, event;
            ~ChildGuard() { if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
                SetEvent(event); WaitForSingleObject(process, INFINITE);
            } }
        } child{process.value,event.value};
        const auto collect = [&] {
            // Publication order is PNG then report: count only completed pairs.
            while (result.completedFrames < result.totalFrames) {
                const int frame = job.startFrame + result.completedFrames;
                if (!std::filesystem::exists(renderJobFramePath(job, frame, ".png"))
                    || !std::filesystem::exists(renderJobFramePath(job, frame, "-report.json"))) break;
                result.lastFrame = frame; ++result.completedFrames; result.outputCount += 2;
                if (progress) progress({frame, result.completedFrames, result.totalFrames,
                    BatchFrameStatus::Complete, "Raster frame published"});
            }
        };
        DWORD wait = WAIT_TIMEOUT;
        do {
            if (cancellation && cancellation->isCancellationRequested()) SetEvent(event.value);
            collect();
            wait = WaitForSingleObject(process.value, 50);
        } while (wait == WAIT_TIMEOUT);
        DWORD code = 1;
        if (wait != WAIT_OBJECT_0 || !GetExitCodeProcess(process.value, &code))
            throw std::runtime_error("Cannot read Raster executor status");
        collect(); // Include any last pair published before exit/cancellation.
        if (code == 0) {
            if(result.completedFrames!=result.totalFrames) throw std::runtime_error("Raster executor exited without all output pairs");
            result.status = BatchFrameStatus::Complete;
        } else if (code == 130) result.status = BatchFrameStatus::Cancelled;
        else { result.failedFrames = 1; result.error = "Raster executor failed (exit " + std::to_string(code) + ")"; }
#else
        result.error = "Raster Queue executor currently requires Windows; use Iris raster-sequence";
#endif
    } catch (const std::exception& exception) { result.error = exception.what(); }
    return result;
}
