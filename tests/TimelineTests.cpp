#include "runtime/Timeline.h"

#include <cmath>
#include <limits>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        Timeline timeline;
        require(timeline.startFrame() == 0, "default start frame must be 0");
        require(timeline.endFrame() == 23, "default end frame must be 23");
        require(timeline.frame() == 0, "default frame must be 0");
        require(timeline.framesPerSecond() == 24, "default FPS must be 24");
        require(timeline.frameCount() == 24, "default frame count must be 24");
        require(!timeline.loop(), "looping must be off by default");

        timeline.setFrameRange(-5, 9);
        require(timeline.startFrame() == 0 && timeline.endFrame() == 9,
                "a negative start frame must clamp to 0");
        timeline.setFrameRange(40, 10);
        require(timeline.startFrame() == 40 && timeline.endFrame() == 40,
                "an inverted range must collapse to its start");
        timeline.setFrame(1000);
        require(timeline.frame() == 40, "scrubbing must clamp into the active range");
        timeline.setFrame(-3);
        require(timeline.frame() == 40, "scrubbing below the range must clamp");

        timeline.setFrameRange(10, 33);
        timeline.setFramesPerSecond(0);
        require(timeline.framesPerSecond() == Timeline::minimumFramesPerSecond,
                "FPS must clamp to the documented minimum");
        timeline.setFramesPerSecond(1000);
        require(timeline.framesPerSecond() == Timeline::maximumFramesPerSecond,
                "FPS must clamp to the documented maximum");
        timeline.setFramesPerSecond(24);
        timeline.setFrame(10);
        require(std::abs(timeline.fixedDeltaSeconds() - 1.0 / 24.0) < 1.0e-12,
                "fixed delta must be the reciprocal of the frame rate");
        require(std::abs(timeline.timeSeconds() - 10.0 / 24.0) < 1.0e-12,
                "time must derive from frame and FPS only");
        require(std::abs(timeline.timeSeconds(33) - 33.0 / 24.0) < 1.0e-12,
                "an explicit frame must map to the same time");

        // A non-looping sequence stops on its last frame instead of running past it.
        timeline.setFrame(31);
        require(timeline.advance(), "advancing inside the range must report a change");
        require(timeline.frame() == 32, "advance must move exactly one frame");
        require(timeline.advance(1), "advancing to the last frame must report a change");
        require(timeline.frame() == 33 && timeline.atEnd(), "advance must reach the last frame");
        require(!timeline.advance(), "advancing past the end must report no change");
        require(timeline.frame() == 33, "a non-looping sequence must stop at the end");
        require(!timeline.advance(0) && !timeline.advance(-4),
                "a non-positive step must be a no-op");

        timeline.setLoop(true);
        require(timeline.advance(), "a looping sequence must wrap from the end");
        require(timeline.frame() == 10, "a looping sequence must wrap to the start frame");
        timeline.setFrame(0);
        require(timeline.frame() == 10, "a looping sequence still clamps scrubs");
        timeline.setFrame(30);
        require(timeline.advance(6), "a multi-frame advance must report a change");
        require(timeline.frame() == 12, "a multi-frame advance must wrap deterministically");

        timeline.reset();
        require(timeline.frame() == 10, "reset must return to the start frame");

        timeline.setFrameRange(0, 23);
        timeline.setFramesPerSecond(24);
        timeline.setFrame(17);
        Timeline copy;
        copy.setFrameRange(0, 23);
        copy.setFramesPerSecond(24);
        copy.setFrame(17);
        require(copy.timeSeconds() == timeline.timeSeconds()
                && copy.fixedDeltaSeconds() == timeline.fixedDeltaSeconds(),
                "identical timelines must agree, independent of how the frame was reached");

        const int maximum = std::numeric_limits<int>::max();
        timeline.setFrameRange(0, maximum);
        require(timeline.frameCount() == static_cast<std::int64_t>(maximum) + 1, "full range count overflowed");
        timeline.setLoop(false); timeline.setFrame(maximum - 1);
        require(timeline.advance(maximum) && timeline.frame() == maximum, "terminal clamp overflowed");
        require(!timeline.advance(), "terminal frame advanced again");
        timeline.setLoop(true);
        require(timeline.advance() && timeline.frame() == 0, "full range loop overflowed");
        timeline.setFrameRange(maximum - 2, maximum); timeline.setFrame(maximum);
        require(timeline.advance(4) && timeline.frame() == maximum - 2, "nonzero-start loop overflowed");
        timeline.setFrameRange(maximum, maximum); timeline.setFrame(maximum);
        require(!timeline.advance(maximum) && timeline.frame() == maximum, "single-frame loop overflowed");
        std::cout << "Deterministic timeline tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Timeline tests failed: " << error.what() << '\n';
        return 1;
    }
}
