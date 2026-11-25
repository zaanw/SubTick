#pragma once

#include <atomic>

namespace subtick {

enum class InputMode {
    Compatibility,
    Exact,
};

class Runtime final {
public:
    static Runtime& get();

    void reloadSettings();
    bool isEnabled() const noexcept;
    bool isActive() const noexcept;
    bool hasConflict() const noexcept;
    InputMode mode() const noexcept;

private:
    std::atomic<bool> m_enabled{true};
    std::atomic<bool> m_conflict{false};
    std::atomic<InputMode> m_mode{InputMode::Compatibility};
};

} // namespace subtick
