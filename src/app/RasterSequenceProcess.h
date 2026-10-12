#pragma once
#include "runtime/BatchRuntime.h"
#include "asset/InputManifest.h"
#include <atomic>
#include <array>
#include <algorithm>
#include <cstring>
#include <vector>
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
            void close() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); value = nullptr; }
            ~Handle() { close(); }
        };
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        Handle reader, writer;
        if (!CreatePipe(&reader.value, &writer.value, &security, 0)
            || !SetHandleInformation(reader.value, HANDLE_FLAG_INHERIT, 0))
            throw std::runtime_error("Cannot create Raster diagnostic pipe");
        Handle input{CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security, OPEN_EXISTING, 0, nullptr)};
        if (input.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open Raster input handle");
        // Drain while the child runs: compiler logs can exceed the pipe capacity.
        // Keep a bounded tail without allocating in exception cleanup.
        struct Diagnostics {
            HANDLE pipe;
            std::array<char, 14336> tail{};
            std::array<char, 2048> head{};
            std::size_t size{0};
            bool truncated{false};
            void drain() noexcept {
                char chunk[4096];
                for (int batch = 0; batch < 64; ++batch) {
                    DWORD available = 0, read = 0;
                    if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || !available
                        || !ReadFile(pipe, chunk, std::min<DWORD>(available, sizeof(chunk)), &read, nullptr) || !read) break;
                    if (size + read > tail.size()) {
                        if (!truncated) std::memcpy(head.data(), tail.data(), head.size());
                        const auto discard = size + read - tail.size();
                        std::memmove(tail.data(), tail.data() + discard, size - discard);
                        size -= discard; truncated = true;
                    }
                    std::memcpy(tail.data() + size, chunk, read); size += read;
                }
            }
            std::string text() const {
                std::size_t first = 0;
                // Truncation may land inside a UTF-8 code point.
                while (first < size && (static_cast<unsigned char>(tail[first]) & 0xc0) == 0x80) ++first;
                std::string prefix;
                if (truncated) {
                    prefix.assign(head.data(), head.size());
                    const auto newline = prefix.rfind('\n');
                    if (newline != std::string::npos) prefix.resize(newline + 1);
                    prefix += "[middle executor output truncated]\n";
                }
                return prefix
                    + std::string(tail.data() + first, size - first);
            }
        } diagnostics{reader.value};
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
        SIZE_T attributeBytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
        std::vector<unsigned char> attributeStorage(attributeBytes);
        const auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());
        if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attributeBytes))
            throw std::runtime_error("Cannot initialize Raster handle inheritance");
        struct AttributeGuard { LPPROC_THREAD_ATTRIBUTE_LIST value;
            ~AttributeGuard() { DeleteProcThreadAttributeList(value); } } attributeGuard{attributes};
        HANDLE inherited[]{writer.value, input.value};
        if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inherited, sizeof(inherited), nullptr, nullptr))
            throw std::runtime_error("Cannot restrict Raster handle inheritance");
        STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = input.value;
        startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = writer.value;
        startup.lpAttributeList = attributes;
        PROCESS_INFORMATION info{};
        if (!CreateProcessW(executable, command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &startup.StartupInfo, &info))
            throw std::runtime_error("Cannot launch Raster executor (Win32 " + std::to_string(GetLastError()) + ")");
        Handle process{info.hProcess}, thread{info.hThread};
        writer.close();
        struct ChildGuard {
            HANDLE process, event;
            Diagnostics& diagnostics;
            ~ChildGuard() { if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
                SetEvent(event);
                do { diagnostics.drain(); } while (WaitForSingleObject(process, 50) == WAIT_TIMEOUT);
            } }
        } child{process.value,event.value,diagnostics};
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
            diagnostics.drain();
            collect();
            wait = WaitForSingleObject(process.value, 50);
        } while (wait == WAIT_TIMEOUT);
        DWORD code = 1;
        if (wait != WAIT_OBJECT_0 || !GetExitCodeProcess(process.value, &code))
            throw std::runtime_error("Cannot read Raster executor status");
        collect(); // Include any last pair published before exit/cancellation.
        diagnostics.drain();
        if (code == 0) {
            if(result.completedFrames!=result.totalFrames) throw std::runtime_error("Raster executor exited without all output pairs");
            result.status = BatchFrameStatus::Complete;
        } else if (code == 130) result.status = BatchFrameStatus::Cancelled;
        else { result.failedFrames = 1; result.error = "Raster executor failed (exit " + std::to_string(code)
            + ")\n" + diagnostics.text(); }
#else
        result.error = "Raster Queue executor currently requires Windows; use Iris raster-sequence";
#endif
    } catch (const std::exception& exception) { result.error = exception.what(); }
    return result;
}
