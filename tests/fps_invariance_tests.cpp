#include "engine/tick_clock.hpp"
#include "engine/timeline.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

using subtick::Timeline;

namespace {
constexpr std::array<double, 10> kInputTimes = {
    1.001, 1.004, 1.0045, 1.03125, 1.123456, 1.25, 1.5, 1.777777, 1.875, 1.999,
};

void require(bool condition, char const* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

std::vector<double> scheduleAtFps(int fps) {
    Timeline timeline;
    for (std::size_t index = 0; index < kInputTimes.size(); ++index) {
        require(timeline.enqueue({
                    .button = static_cast<std::int32_t>(index + 1),
                    .timestamp = kInputTimes[index],
                }),
                "enqueue frame-rate invariant input");
    }

    std::vector<double> observed(kInputTimes.size(), -1.0);
    constexpr double tickDuration = 1.0 / 240.0;
    double tickStart = 1.0;

    for (int frame = 1; frame <= fps; ++frame) {
        auto const horizon = 1.0 + static_cast<double>(frame) / static_cast<double>(fps);

        while (tickStart + tickDuration <= horizon + 1e-12) {
            require(timeline.beginTick(tickStart, tickDuration, horizon),
                    "prepare actual physics tick");

            for (auto const& scheduled : timeline.scheduledInputs()) {
                auto const index = static_cast<std::size_t>(scheduled.input.button - 1);
                observed[index] = tickStart + scheduled.fraction * tickDuration;
            }

            require(timeline.commitTick(), "commit actual physics tick");
            tickStart += tickDuration;
        }
    }

    require(std::abs(tickStart - 2.0) < 1e-9, "simulated ticks cover exactly one second");
    require(timeline.pendingCount() == 0, "all inputs are resolved by the final horizon");
    return observed;
}
} // namespace

int main() {
    auto const normal = subtick::tickDurationFromStep(0.25f, 1.0f);
    require(normal && std::abs(*normal - 1.0 / 240.0) < 1e-9,
            "normalized physics step maps to 240 Hz");

    constexpr std::array fpsValues = {
        15, 24, 30, 40, 50, 60, 75, 80, 90, 120, 144, 165, 240, 360, 500, 1000,
    };

    auto const reference = scheduleAtFps(240);
    for (auto const fps : fpsValues) {
        auto const observed = scheduleAtFps(fps);
        for (std::size_t index = 0; index < observed.size(); ++index) {
            require(observed[index] >= 0.0, "input was scheduled");
            require(std::abs(observed[index] - reference[index]) < 1e-9,
                    "same input timestamp maps to same simulation position");
            require(std::abs(observed[index] - kInputTimes[index]) < 1e-9,
                    "scheduled position equals source timestamp");
        }
    }

    std::cout << "SubTick FPS invariance passed from 15 to 1000 FPS\n";
    return 0;
}
