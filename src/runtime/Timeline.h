#pragma once

#include <algorithm>
#include <cstdint>

// Deterministic frame/time mapping shared by the GUI preview and the headless
// batch runtime.
//
// A timestamp is always derived from the frame index and the fixed frame rate:
// nothing here reads wall-clock time or the GUI frame rate, so scrubbing to a
// frame, a single step and a batch frame all produce the same time, and two runs
// of the same frame always produce the same output. The class is intentionally
// self-contained so the editor, the runtime library and CPU-only tests can share
// exactly one definition without adding a build dependency.
class Timeline {
public:
    static constexpr int minimumFramesPerSecond = 1;
    static constexpr int maximumFramesPerSecond = 240;

    int startFrame() const { return startFrame_; }
    int endFrame() const { return endFrame_; }
    int frame() const { return frame_; }
    int framesPerSecond() const { return framesPerSecond_; }
    bool loop() const { return loop_; }

    std::int64_t frameCount() const { return static_cast<std::int64_t>(endFrame_) - startFrame_ + 1; }
    bool atEnd() const { return frame_ >= endFrame_; }

    void setFrameRange(int startFrame, int endFrame) {
        startFrame_ = std::max(startFrame, 0);
        endFrame_ = std::max(endFrame, startFrame_);
        frame_ = std::clamp(frame_, startFrame_, endFrame_);
    }

    void setFramesPerSecond(int framesPerSecond) {
        framesPerSecond_ = std::clamp(
            framesPerSecond, minimumFramesPerSecond, maximumFramesPerSecond
        );
    }

    void setLoop(bool loop) { loop_ = loop; }

    // Scrub: an explicit frame request is clamped into the active range.
    void setFrame(int frame) { frame_ = std::clamp(frame, startFrame_, endFrame_); }

    // Advances by whole frames. Returns true when the frame actually changed. A
    // non-looping sequence stops on its last frame instead of running past it, so a
    // caller can use the return value as its termination condition.
    bool advance(int frames = 1) {
        if (frames <= 0) return false;
        const std::int64_t target = static_cast<std::int64_t>(frame_) + frames;
        std::int64_t next = target;
        if (target > endFrame_) {
            if (loop_) {
                const auto count = frameCount();
                next = startFrame_ + ((target - startFrame_) % count);
            } else {
                next = endFrame_;
            }
        }
        if (next == frame_) return false;
        frame_ = static_cast<int>(next);
        return true;
    }

    void reset() { frame_ = startFrame_; }

    double fixedDeltaSeconds() const {
        return 1.0 / static_cast<double>(framesPerSecond_);
    }

    double timeSeconds() const { return timeSeconds(frame_); }

    double timeSeconds(int frame) const {
        return static_cast<double>(frame) / static_cast<double>(framesPerSecond_);
    }

private:
    int startFrame_{0};
    int endFrame_{23};
    int frame_{0};
    int framesPerSecond_{24};
    bool loop_{false};
};
