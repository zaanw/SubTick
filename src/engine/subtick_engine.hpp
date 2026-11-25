#pragma once

#include "timeline.hpp"

#include <cstdint>
#include <span>

#include <Geode/Geode.hpp>

class PlayLayer;

namespace subtick {

struct EngineMetrics {
    std::uint64_t framesScheduled = 0;
    std::uint64_t splitTicks = 0;
    std::uint64_t inputsApplied = 0;
    std::uint64_t fallbackFrames = 0;
    std::uint64_t invalidTimingFallbacks = 0;
    std::uint64_t queueOverflows = 0;
    std::uint64_t lateInputs = 0;
    std::uint64_t clockResyncs = 0;
    std::uint32_t queueHighWatermark = 0;
    double lastFrameDurationMs = 0.0;
    double lastClockDriftMs = 0.0;
    double maximumClockDriftMs = 0.0;
    double lastApplyDelayMs = 0.0;
    double averageApplyDelayMs = 0.0;
    double maximumApplyDelayMs = 0.0;
    std::uint64_t applyDelaySamples = 0;
};

class SubtickEngine final {
public:
    static SubtickEngine& get();

    void resetForLayer(PlayLayer* layer);
    bool beginFrame(PlayLayer* layer, double modifiedDelta);
    bool beginTick(PlayLayer* layer, float stepDelta);
    bool commitTick();
    void flushToVanilla(PlayLayer* layer);

    std::span<ScheduledInput const> scheduledInputs() const noexcept;
    void recordSplitTick() noexcept;
    void recordInputApplied(double inputTimestamp) noexcept;
    EngineMetrics metrics() const noexcept;

private:
    enum class CaptureResult {
        Success,
        Overflow,
        InvalidTimestamp,
    };

    double now() const;
    CaptureResult captureQueuedInputs(PlayLayer* layer);
    void requeueUnresolvedInputs(PlayLayer* layer);
    void enableNativeFallback(PlayLayer* layer) noexcept;
    void primeTimeline(PlayLayer* layer, double boundary) noexcept;

    Timeline m_timeline;
    EngineMetrics m_metrics;
    PlayLayer* m_layer = nullptr;
    double m_lastBoundary = 0.0;
    double m_nextTickStart = 0.0;
    double m_inputHorizon = 0.0;
    double m_preparedTickDuration = 0.0;
    std::uint32_t m_ticksThisFrame = 0;
    bool m_frameArmed = false;
    bool m_tickPrepared = false;
};

} // namespace subtick
