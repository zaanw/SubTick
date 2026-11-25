#include "subtick_engine.hpp"

#include "tick_clock.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace subtick {
namespace {
constexpr double kMaximumFrameDuration = 0.25;
constexpr double kMaximumClockDrift = (1.0 / 240.0) * 2.0;

double sortableTimestamp(double timestamp) {
    return std::isfinite(timestamp) && timestamp > 0.0
        ? timestamp
        : -std::numeric_limits<double>::infinity();
}

InputRecord toInputRecord(PlayerButtonCommand const& command) {
    return {
        .button = static_cast<std::int32_t>(command.m_button),
        .originalStep = command.m_step,
        .push = command.m_isPush,
        .player2 = command.m_isPlayer2,
        .timestamp = command.m_timestamp,
    };
}

PlayerButtonCommand toPlayerCommand(InputRecord const& input) {
    return {
        .m_button = static_cast<PlayerButton>(input.button),
        .m_isPush = input.push,
        .m_isPlayer2 = input.player2,
        .m_step = input.originalStep,
        .m_timestamp = input.timestamp,
    };
}
} // namespace

SubtickEngine& SubtickEngine::get() {
    static SubtickEngine instance;
    return instance;
}

void SubtickEngine::resetForLayer(PlayLayer* layer) {
    auto const boundary = layer ? now() : 0.0;
    primeTimeline(layer, boundary);
}

bool SubtickEngine::beginFrame(PlayLayer* layer, double modifiedDelta) {
    if (!layer || !std::isfinite(modifiedDelta)) {
        return false;
    }

    if (m_layer != layer) {
        resetForLayer(layer);
    }

    auto const boundary = now();
    if (!std::isfinite(boundary) || boundary <= 0.0 || m_lastBoundary <= 0.0) {
        ++m_metrics.fallbackFrames;
        ++m_metrics.invalidTimingFallbacks;
        flushToVanilla(layer);
        return false;
    }

    auto const frameDuration = boundary - m_lastBoundary;
    m_metrics.lastFrameDurationMs = frameDuration * 1000.0;

    if (!std::isfinite(frameDuration) || frameDuration <= 0.0 ||
        frameDuration > kMaximumFrameDuration || m_tickPrepared ||
        m_timeline.tickPrepared()) {
        ++m_metrics.fallbackFrames;
        ++m_metrics.invalidTimingFallbacks;
        flushToVanilla(layer);
        return false;
    }

    if (m_frameArmed) {
        auto const drift = m_inputHorizon - m_nextTickStart;
        auto const absoluteDrift = std::abs(drift);
        m_metrics.lastClockDriftMs = drift * 1000.0;
        m_metrics.maximumClockDriftMs =
            std::max(m_metrics.maximumClockDriftMs, absoluteDrift * 1000.0);

        if (!std::isfinite(drift) || absoluteDrift > kMaximumClockDrift) {
            ++m_metrics.fallbackFrames;
            ++m_metrics.invalidTimingFallbacks;
            ++m_metrics.clockResyncs;
            flushToVanilla(layer);
            return false;
        }
    }

    auto const capture = captureQueuedInputs(layer);
    if (capture != CaptureResult::Success) {
        ++m_metrics.fallbackFrames;
        if (capture == CaptureResult::Overflow) {
            ++m_metrics.queueOverflows;
        } else {
            ++m_metrics.invalidTimingFallbacks;
        }
        flushToVanilla(layer);
        return false;
    }

    m_lastBoundary = boundary;
    m_inputHorizon = boundary;
    m_ticksThisFrame = 0;
    m_frameArmed = true;

    layer->m_clickBetweenSteps = false;
    layer->m_clickOnSteps = false;

    ++m_metrics.framesScheduled;
    m_metrics.queueHighWatermark = std::max(
        m_metrics.queueHighWatermark,
        static_cast<std::uint32_t>(m_timeline.highWatermark())
    );
    return true;
}

bool SubtickEngine::beginTick(PlayLayer* layer, float stepDelta) {
    if (!layer || layer != m_layer || !m_frameArmed || m_tickPrepared) {
        if (layer) {
            ++m_metrics.fallbackFrames;
            ++m_metrics.invalidTimingFallbacks;
            flushToVanilla(layer);
        }
        return false;
    }

    auto const duration = tickDurationFromStep(stepDelta, layer->m_gameState.m_timeWarp);
    if (!duration || *duration > kMaximumFrameDuration ||
        !m_timeline.beginTick(m_nextTickStart, *duration, m_inputHorizon)) {
        ++m_metrics.fallbackFrames;
        ++m_metrics.invalidTimingFallbacks;
        flushToVanilla(layer);
        return false;
    }

    m_metrics.lateInputs += m_timeline.lastLateInputCount();
    m_preparedTickDuration = *duration;
    m_tickPrepared = true;
    return true;
}

bool SubtickEngine::commitTick() {
    if (!m_tickPrepared || !m_timeline.commitTick()) {
        return false;
    }

    m_nextTickStart += m_preparedTickDuration;
    ++m_ticksThisFrame;
    m_preparedTickDuration = 0.0;
    m_tickPrepared = false;
    return true;
}

void SubtickEngine::flushToVanilla(PlayLayer* layer) {
    if (!layer) {
        m_timeline.reset();
        m_layer = nullptr;
        m_frameArmed = false;
        m_tickPrepared = false;
        return;
    }

    requeueUnresolvedInputs(layer);
    enableNativeFallback(layer);
    primeTimeline(layer, now());
}

std::span<ScheduledInput const> SubtickEngine::scheduledInputs() const noexcept {
    return m_timeline.scheduledInputs();
}

void SubtickEngine::recordSplitTick() noexcept {
    ++m_metrics.splitTicks;
}

void SubtickEngine::recordInputApplied(double inputTimestamp) noexcept {
    ++m_metrics.inputsApplied;
    if (!std::isfinite(inputTimestamp) || inputTimestamp <= 0.0) {
        return;
    }

    auto const delay = (now() - inputTimestamp) * 1000.0;
    if (!std::isfinite(delay) || delay < 0.0 || delay > 1000.0) {
        return;
    }

    ++m_metrics.applyDelaySamples;
    m_metrics.lastApplyDelayMs = delay;
    m_metrics.averageApplyDelayMs +=
        (delay - m_metrics.averageApplyDelayMs) /
        static_cast<double>(m_metrics.applyDelaySamples);
    m_metrics.maximumApplyDelayMs = std::max(m_metrics.maximumApplyDelayMs, delay);
}

EngineMetrics SubtickEngine::metrics() const noexcept {
    return m_metrics;
}

double SubtickEngine::now() const {
    return geode::utils::getInputTimestamp();
}

SubtickEngine::CaptureResult SubtickEngine::captureQueuedInputs(PlayLayer* layer) {
    auto const inputCount = layer->m_queuedButtons.size();
    if (inputCount > m_timeline.availableCapacity()) {
        return CaptureResult::Overflow;
    }

    for (auto const& command : layer->m_queuedButtons) {
        if (!std::isfinite(command.m_timestamp) || command.m_timestamp <= 0.0) {
            return CaptureResult::InvalidTimestamp;
        }
    }

    for (auto const& command : layer->m_queuedButtons) {
        if (!m_timeline.enqueue(toInputRecord(command))) {
            return CaptureResult::Overflow;
        }
    }

    layer->m_queuedButtons.clear();
    return CaptureResult::Success;
}

void SubtickEngine::requeueUnresolvedInputs(PlayLayer* layer) {
    auto const scheduled = m_timeline.scheduledInputs();
    auto const pending = m_timeline.pendingInputs();
    if (scheduled.empty() && pending.empty()) {
        return;
    }

    gd::vector<PlayerButtonCommand> merged;
    merged.reserve(scheduled.size() + pending.size() + layer->m_queuedButtons.size());

    for (auto const& item : scheduled) {
        merged.push_back(toPlayerCommand(item.input));
    }
    for (auto const& item : pending) {
        merged.push_back(toPlayerCommand(item));
    }
    for (auto const& item : layer->m_queuedButtons) {
        merged.push_back(item);
    }

    std::stable_sort(merged.begin(), merged.end(), [](auto const& left, auto const& right) {
        return sortableTimestamp(left.m_timestamp) < sortableTimestamp(right.m_timestamp);
    });

    layer->m_queuedButtons = std::move(merged);
}

void SubtickEngine::enableNativeFallback(PlayLayer* layer) noexcept {
    layer->m_clickBetweenSteps = true;
    layer->m_clickOnSteps = false;
}

void SubtickEngine::primeTimeline(PlayLayer* layer, double boundary) noexcept {
    m_timeline.reset();
    m_layer = layer;
    m_lastBoundary = layer ? boundary : 0.0;
    m_nextTickStart = layer ? boundary : 0.0;
    m_inputHorizon = layer ? boundary : 0.0;
    m_preparedTickDuration = 0.0;
    m_ticksThisFrame = 0;
    m_frameArmed = false;
    m_tickPrepared = false;
}

} // namespace subtick
