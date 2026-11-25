#include "engine/timeline.hpp"
#include "engine/tick_clock.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using subtick::InputRecord;
using subtick::Timeline;

namespace {
void require(bool condition, char const* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

InputRecord input(std::int32_t button, double timestamp, bool push = true) {
    return {
        .button = button,
        .push = push,
        .timestamp = timestamp,
    };
}

void testStableOrderingAndCommit() {
    Timeline timeline;
    require(timeline.enqueue(input(1, 1.125)), "enqueue first same-time input");
    require(timeline.enqueue(input(2, 1.125)), "enqueue second same-time input");
    require(timeline.enqueue(input(3, 1.0625)), "enqueue earlier input");
    require(timeline.beginTick(1.0, 0.25, 1.30), "prepare tick");

    auto const scheduled = timeline.scheduledInputs();
    require(scheduled.size() == 3, "all in-range inputs are scheduled");
    require(scheduled[0].input.button == 3, "timestamps are sorted ascending");
    require(scheduled[1].input.button == 1, "same-time inputs preserve insertion order");
    require(scheduled[2].input.button == 2, "same-time second input remains second");
    require(scheduled[1].fraction == scheduled[2].fraction, "equal timestamps share a fraction");
    require(timeline.commitTick(), "commit prepared tick");
    require(timeline.scheduledCount() == 0, "commit clears scheduled inputs");
    require(!timeline.commitTick(), "double commit is rejected");
}

void testRightBoundaryBelongsToNextTick() {
    Timeline timeline;
    require(timeline.enqueue(input(7, 1.25)), "enqueue boundary input");
    require(timeline.beginTick(1.0, 0.25, 1.50), "prepare first tick");
    require(timeline.scheduledCount() == 0, "future boundary input is not pulled early");
    require(timeline.pendingCount() == 1, "boundary input remains pending");
    require(timeline.commitTick(), "commit empty tick");

    require(timeline.beginTick(1.25, 0.25, 1.50), "prepare second tick");
    auto const scheduled = timeline.scheduledInputs();
    require(scheduled.size() == 1, "boundary input enters next tick");
    require(std::abs(scheduled[0].fraction) < 1e-12, "boundary input is at fraction zero");
}

void testLateInputsClampToTickStart() {
    Timeline timeline;
    require(timeline.enqueue(input(9, 1.10)), "enqueue late input");
    require(timeline.beginTick(1.20, 0.10, 1.30), "prepare tick with late input");
    auto const scheduled = timeline.scheduledInputs();
    require(scheduled.size() == 1, "late input is still resolved");
    require(scheduled[0].fraction == 0.0, "late input clamps to tick start");
    require(timeline.lastLateInputCount() == 1, "late input is counted");
}

void testInputHorizonDefersFutureInput() {
    Timeline timeline;
    require(timeline.enqueue(input(4, 1.20)), "enqueue future input");
    require(timeline.beginTick(1.10, 0.20, 1.15), "prepare with short input horizon");
    require(timeline.scheduledCount() == 0, "input beyond current horizon is not scheduled");
    require(timeline.pendingCount() == 1, "future input remains pending");
    require(timeline.commitTick(), "commit horizon-limited tick");
}

void testCapacityAndInvalidInputs() {
    Timeline timeline;
    require(!timeline.enqueue(input(1, 0.0)), "zero timestamp is rejected");
    require(!timeline.enqueue(input(2, std::numeric_limits<double>::infinity())),
            "infinite timestamp is rejected");
    require(!timeline.enqueue(input(3, std::numeric_limits<double>::quiet_NaN())),
            "NaN timestamp is rejected");

    for (std::size_t index = 0; index < subtick::kMaxBufferedInputs; ++index) {
        require(timeline.enqueue(input(static_cast<std::int32_t>(index), 10.0 + index)),
                "fill fixed-capacity queue");
    }
    require(timeline.availableCapacity() == 0, "queue reports no remaining capacity");
    require(!timeline.enqueue(input(9999, 9999.0)), "overflow is rejected");
    require(timeline.highWatermark() == subtick::kMaxBufferedInputs,
            "high-water mark records maximum queue occupancy");
}

void testInvalidTickDoesNotMutateQueue() {
    Timeline timeline;
    require(timeline.enqueue(input(1, 1.5)), "enqueue before invalid tick");
    require(!timeline.beginTick(0.0, 0.25, 2.0), "zero tick start is rejected");
    require(!timeline.beginTick(1.0, 0.0, 2.0), "zero duration is rejected");
    require(!timeline.beginTick(1.0, 0.25, 0.5), "horizon before tick start is rejected");
    require(!timeline.beginTick(1.0, std::numeric_limits<double>::infinity(), 2.0),
            "infinite duration is rejected");
    require(timeline.pendingCount() == 1, "invalid tick leaves pending input untouched");
    require(timeline.scheduledCount() == 0, "invalid tick creates no schedule");
}

void testNoEnqueueDuringPreparedTick() {
    Timeline timeline;
    require(timeline.enqueue(input(1, 1.25)), "enqueue before prepare");
    require(timeline.beginTick(1.0, 0.3, 1.5), "prepare tick");
    require(!timeline.enqueue(input(2, 1.3)), "enqueue during prepared tick is rejected");
    require(timeline.commitTick(), "commit prepared tick");
    require(timeline.enqueue(input(2, 1.3)), "enqueue after commit succeeds");
}

void testTickClockValidation() {
    auto const normal = subtick::tickDurationFromStep(0.25f, 1.0f);
    require(normal && std::abs(*normal - (1.0 / 240.0)) < 1e-9,
            "normal game delta maps to 240 Hz");
    auto const halfSpeed = subtick::tickDurationFromStep(0.125f, 0.5f);
    require(halfSpeed && std::abs(*halfSpeed - (1.0 / 240.0)) < 1e-9,
            "time warp is removed from tick clock");
    require(!subtick::tickDurationFromStep(0.0f, 1.0f), "zero step delta is rejected");
    require(!subtick::tickDurationFromStep(0.25f, 0.0f), "zero time warp is rejected");
}
} // namespace

int main() {
    testStableOrderingAndCommit();
    testRightBoundaryBelongsToNextTick();
    testLateInputsClampToTickStart();
    testInputHorizonDefersFutureInput();
    testCapacityAndInvalidInputs();
    testInvalidTickDoesNotMutateQueue();
    testNoEnqueueDuringPreparedTick();
    testTickClockValidation();
    std::cout << "SubTick timeline tests passed\n";
    return 0;
}
