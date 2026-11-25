#include "runtime.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace subtick {
namespace {
bool hasInputConflict() {
    auto const loader = Loader::get();
    return loader->getLoadedMod("syzzi.click_between_frames") != nullptr ||
           loader->getLoadedMod("zmx.cbf-lite") != nullptr ||
           loader->getLoadedMod("wayago.click-between-ticks") != nullptr;
}
} // namespace

Runtime& Runtime::get() {
    static Runtime instance;
    return instance;
}

void Runtime::reloadSettings() {
    auto const mod = Mod::get();
    m_enabled.store(mod->getSettingValue<bool>("enabled"), std::memory_order_relaxed);

    auto const selectedMode = mod->getSettingValue<std::string>("mode");
    m_mode.store(
        selectedMode == "Exact Subtick" ? InputMode::Exact : InputMode::Compatibility,
        std::memory_order_relaxed
    );
    m_conflict.store(hasInputConflict(), std::memory_order_relaxed);
}

bool Runtime::isEnabled() const noexcept {
    return m_enabled.load(std::memory_order_relaxed);
}

bool Runtime::isActive() const noexcept {
    return isEnabled() && !hasConflict();
}

bool Runtime::hasConflict() const noexcept {
    return m_conflict.load(std::memory_order_relaxed);
}

InputMode Runtime::mode() const noexcept {
    return m_mode.load(std::memory_order_relaxed);
}

} // namespace subtick

$on_mod(Loaded) {
    subtick::Runtime::get().reloadSettings();

    listenForSettingChanges<bool>("enabled", [](bool) {
        subtick::Runtime::get().reloadSettings();
    });

    listenForSettingChanges<std::string>("mode", [](std::string) {
        subtick::Runtime::get().reloadSettings();
    });
}

$on_game(ModsLoaded) {
    subtick::Runtime::get().reloadSettings();
    if (subtick::Runtime::get().hasConflict()) {
        log::warn("SubTick is inactive because another input-timing mod is loaded.");
    }
}
