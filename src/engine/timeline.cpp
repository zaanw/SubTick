#include "timeline.hpp"

#include <algorithm>
#include <cmath>

namespace subtick {

bool Timeline::enqueue(InputRecord input) noexcept {
    if (!std::isfinite(input.timestamp) || input.timestamp <= 0.0 ||
        m_pendingCount == m_pending.size() || m_tickPrepared) {
        return false;
    }

    if (input.sequence == 0) {
        input.sequence = m_nextSequence++;
        if (m_nextSequence == 0) {
            m_nextSequence = 1;
        }
    }

    m_pending[m_pendingCount++] = input;
    m_highWatermark = std::max(m_highWatermark, m_pendingCount);
    return true;
}

bool Timeline::beginTick(double tickStart, double tickDuration, double inputHorizon) noexcept {
    if (m_tickPrepared || !std::isfinite(tickStart) || !std::isfinite(tickDuration) ||
        !std::isfinite(inputHorizon) || tickStart <= 0.0 || tickDuration <= 0.0 ||
        inputHorizon < tickStart) {
        return false;
    }

    auto const tickEnd = tickStart + tickDuration;
    if (!std::isfinite(tickEnd) || tickEnd <= tickStart) {
        return false;
    }

    m_scheduledCount = 0;
    m_lastLateInputCount = 0;

    std::sort(m_pending.begin(), m_pending.begin() + m_pendingCount,
              [](InputRecord const& left, InputRecord const& right) {
                  if (left.timestamp == right.timestamp) {
                      return left.sequence < right.sequence;
                  }
                  return left.timestamp < right.timestamp;
              });

    auto const schedulingBoundary = std::min(tickEnd, inputHorizon);
    std::size_t retainedCount = 0;
    auto const lastRepresentableFraction = std::nextafter(1.0, 0.0);

    for (std::size_t index = 0; index < m_pendingCount; ++index) {
        auto const input = m_pending[index];

        // Inputs exactly on the right boundary belong to the next tick.
        if (input.timestamp >= schedulingBoundary) {
            m_pending[retainedCount++] = input;
            continue;
        }

        double fraction = 0.0;
        if (input.timestamp < tickStart) {
            ++m_lastLateInputCount;
        } else {
            fraction = (input.timestamp - tickStart) / tickDuration;
        }

        fraction = std::clamp(fraction, 0.0, lastRepresentableFraction);
        m_scheduled[m_scheduledCount++] = {
            .input = input,
            .fraction = fraction,
        };
    }

    m_pendingCount = retainedCount;
    m_tickPrepared = true;
    return true;
}

std::span<ScheduledInput const> Timeline::scheduledInputs() const noexcept {
    return std::span<ScheduledInput const>(m_scheduled).first(m_scheduledCount);
}

std::span<InputRecord const> Timeline::pendingInputs() const noexcept {
    return std::span<InputRecord const>(m_pending).first(m_pendingCount);
}

bool Timeline::commitTick() noexcept {
    if (!m_tickPrepared) {
        return false;
    }

    m_scheduledCount = 0;
    m_tickPrepared = false;
    return true;
}

void Timeline::reset() noexcept {
    m_pendingCount = 0;
    m_scheduledCount = 0;
    m_highWatermark = 0;
    m_lastLateInputCount = 0;
    m_nextSequence = 1;
    m_tickPrepared = false;
}

std::size_t Timeline::pendingCount() const noexcept {
    return m_pendingCount;
}

std::size_t Timeline::scheduledCount() const noexcept {
    return m_scheduledCount;
}

std::size_t Timeline::availableCapacity() const noexcept {
    return m_pending.size() - m_pendingCount;
}

std::size_t Timeline::highWatermark() const noexcept {
    return m_highWatermark;
}

std::size_t Timeline::lastLateInputCount() const noexcept {
    return m_lastLateInputCount;
}

bool Timeline::tickPrepared() const noexcept {
    return m_tickPrepared;
}

} // namespace subtick
