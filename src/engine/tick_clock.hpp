#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

namespace subtick {

inline constexpr double kGameplayDeltaUnitsPerSecond = 60.0;

inline std::optional<double> tickDurationFromStep(float stepDelta, float timeWarp) noexcept {
    if (!std::isfinite(stepDelta) || stepDelta <= 0.0f || !std::isfinite(timeWarp)) {
        return std::nullopt;
    }

    auto const normalizedTimeWarp = std::min(1.0f, timeWarp);
    if (normalizedTimeWarp <= 0.0f) {
        return std::nullopt;
    }

    auto const duration =
        static_cast<double>(stepDelta) /
        (kGameplayDeltaUnitsPerSecond * static_cast<double>(normalizedTimeWarp));

    if (!std::isfinite(duration) || duration <= 0.0) {
        return std::nullopt;
    }

    return duration;
}

} // namespace subtick
