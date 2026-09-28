#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
namespace capture {
struct InputFile {
    std::filesystem::path path;
    bool exists{false};
    std::uint64_t bytes{0};
    std::string fingerprint;
};
inline std::string fingerprintBytes(const std::string& bytes) {
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : bytes) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    std::ostringstream s;
    s << std::hex << std::setw(16) << std::setfill('0') << h;
    return s.str();
}
inline InputFile snapshot(const std::filesystem::path& path) {
    InputFile result;
    result.path = std::filesystem::absolute(path).lexically_normal();
    result.exists = std::filesystem::is_regular_file(result.path);
    if (!result.exists)
        return result;
    std::ifstream file(result.path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot read capture input: " + result.path.string());
    std::uint64_t h = 14695981039346656037ULL;
    char block[65536];
    while (file) {
        file.read(block, sizeof(block));
        result.bytes += static_cast<std::uint64_t>(file.gcount());
        for (std::streamsize i = 0; i < file.gcount(); ++i) {
            h ^= static_cast<unsigned char>(block[i]);
            h *= 1099511628211ULL;
        }
    }
    if (!file.eof())
        throw std::runtime_error("Cannot fingerprint capture input: " + result.path.string());
    std::ostringstream s;
    s << std::hex << std::setw(16) << std::setfill('0') << h;
    result.fingerprint = s.str();
    return result;
}
inline bool same(const InputFile& a, const InputFile& b) {
    return a.exists == b.exists && a.bytes == b.bytes && a.fingerprint == b.fingerprint;
}
class InputManifest;
inline thread_local InputManifest* activeManifest = nullptr;
class InputManifest {
  public:
    InputManifest() {
        if (activeManifest)
            throw std::runtime_error("Nested input capture");
        activeManifest = this;
    }
    ~InputManifest() { activeManifest = nullptr; }
    InputManifest(const InputManifest&) = delete;
    InputManifest& operator=(const InputManifest&) = delete;
    void record(const std::filesystem::path& path) {
        if (path.empty())
            return;
        const auto entry = snapshot(path);
        const auto key = entry.path.generic_string();
        const auto found = files.find(key);
        if (found != files.end() && !same(found->second, entry))
            throw std::runtime_error("Capture input changed: " + key);
        files.emplace(key, entry);
    }
    void validate() const {
        for (const auto& pair : files)
            if (!same(pair.second, snapshot(pair.second.path)))
                throw std::runtime_error("Capture input changed: " + pair.first);
    }
    bool runtimeBinariesCaptured{false};
    std::map<std::string, InputFile> files;
};
inline void recordInput(const std::filesystem::path& path) {
    if (activeManifest)
        activeManifest->record(path);
}
} // namespace capture
