#pragma once
#include <algorithm>
#include <filesystem>
#include <vector>

namespace capture {
struct InputStamp;
// Context-thread dependency discovery, including inputs rejected before they can be read.
class InputWatch;
inline thread_local InputWatch* activeInputWatch = nullptr;
class InputWatch {
public:
    InputWatch() : previous_(activeInputWatch) { activeInputWatch = this; }
    ~InputWatch() { activeInputWatch = previous_; }
    InputWatch(const InputWatch&) = delete;
    InputWatch& operator=(const InputWatch&) = delete;
    void record(const std::filesystem::path& path);
    std::vector<std::filesystem::path> paths;
    std::vector<InputStamp> stamps;
private:
    InputWatch* previous_;
};
inline void watchInput(const std::filesystem::path& path) {
    if (activeInputWatch) activeInputWatch->record(path);
}
struct InputStamp {
    std::filesystem::path path;
    std::filesystem::file_time_type time{};
    std::uintmax_t bytes{0};
    bool exists{false};
    static InputStamp read(const std::filesystem::path& path) {
        InputStamp result; result.path = path;
        std::error_code error;
        result.exists = std::filesystem::exists(path, error);
        if (result.exists) {
            result.time = std::filesystem::last_write_time(path, error);
            if (error) result.time = std::filesystem::file_time_type::min();
            if (std::filesystem::is_regular_file(path, error)) {
                result.bytes = std::filesystem::file_size(path, error);
                if (error) result.bytes = 0;
            }
        }
        return result;
    }
    bool changed() const {
        const auto current = read(path);
        return exists != current.exists || time != current.time || bytes != current.bytes;
    }
};
inline void InputWatch::record(const std::filesystem::path& path) {
    if (path.empty()) return;
    const auto normalized = std::filesystem::absolute(path).lexically_normal();
    if (std::find(paths.begin(), paths.end(), normalized) == paths.end()) {
        paths.push_back(normalized); stamps.push_back(InputStamp::read(normalized));
    }
    if (previous_) previous_->record(normalized);
}
}
