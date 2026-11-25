#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace subtick {

inline constexpr std::size_t kMaxBufferedInputs = 2048;

struct InputRecord {
    std::int32_t button = 0;
    std::int32_t originalStep = 0;
    bool push = false;
    bool player2 = false;
    double timestamp = 0.0;
    std::uint64_t sequence = 0;
};

struct ScheduledInput {
    InputRecord input;
    double fraction = 0.0;
};

class Timeline final {
public:
    bool enqueue(InputRecord input) noexcept;
    bool beginTick(double tickStart, double tickDuration, double inputHorizon) noexcept;
    std::span<ScheduledInput const> scheduledInputs() const noexcept;
    std::span<InputRecord const> pendingInputs() const noexcept;
    bool commitTick() noexcept;
    void reset() noexcept;

    std::size_t pendingCount() const noexcept;
    std::size_t scheduledCount() const noexcept;
    std::size_t availableCapacity() const noexcept;
    std::size_t highWatermark() const noexcept;
    std::size_t lastLateInputCount() const noexcept;
    bool tickPrepared() const noexcept;

private:
    std::array<InputRecord, kMaxBufferedInputs> m_pending{};
    std::array<ScheduledInput, kMaxBufferedInputs> m_scheduled{};
    std::size_t m_pendingCount = 0;
    std::size_t m_scheduledCount = 0;
    std::size_t m_highWatermark = 0;
    std::size_t m_lastLateInputCount = 0;
    std::uint64_t m_nextSequence = 1;
    bool m_tickPrepared = false;
};

} // namespace subtick
