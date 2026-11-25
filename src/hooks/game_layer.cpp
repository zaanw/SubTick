#include "../engine/subtick_engine.hpp"
#include "../runtime.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

#include <algorithm>
#include <cmath>

#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#endif

using namespace geode::prelude;

namespace {
using subtick::InputMode;
using subtick::Runtime;
using subtick::SubtickEngine;

PlayLayer* s_engineLayer = nullptr;
bool s_timelineActive = false;
bool s_midStep = false;
bool s_skipNextPlayer2Update = false;
bool s_player1Split = false;
bool s_player2Split = false;
bool s_player1ShipRotationDeferred = false;
bool s_player2ShipRotationDeferred = false;
bool s_replayingShipRotation = false;
CCPoint s_player1Position;
CCPoint s_player2Position;
float s_finalRotationDelta = 0.0f;
float s_fullStepDelta = 0.0f;

void clearSplitState() {
    s_midStep = false;
    s_skipNextPlayer2Update = false;
    s_player1Split = false;
    s_player2Split = false;
    s_player1ShipRotationDeferred = false;
    s_player2ShipRotationDeferred = false;
    s_replayingShipRotation = false;
    s_finalRotationDelta = 0.0f;
    s_fullStepDelta = 0.0f;
}

bool isCurrentPlayLayer(GJBaseGameLayer* gameLayer, PlayLayer* playLayer) {
    return playLayer && static_cast<GJBaseGameLayer*>(playLayer) == gameLayer &&
           playLayer->getParent() != nullptr;
}

bool canScheduleSubticks(GJBaseGameLayer* gameLayer, PlayLayer* playLayer) {
    if (!isCurrentPlayLayer(gameLayer, playLayer) || playLayer->m_playerDied ||
        GameManager::sharedState()->getEditorLayer() != nullptr) {
        return false;
    }

#ifdef GEODE_IS_WINDOWS
    if (GetFocus() == nullptr) {
        return false;
    }
#endif

    return playLayer->getParent()->getChildByType<PauseLayer>(0) == nullptr &&
           playLayer->getChildByType<EndLevelLayer>(0) == nullptr;
}

bool canSplitPlayer(PlayerObject* player) {
    return player && (player->m_isOnGround ||
                      (player->m_touchingRings && player->m_touchingRings->count() != 0) ||
                      player->m_isDashing || player->m_isDart || player->m_isBird ||
                      player->m_isShip || player->m_isSwing);
}

bool shouldSplitPlayer(PlayerObject* player, InputMode mode) {
    return player && (mode == InputMode::Exact || canSplitPlayer(player));
}

void resetCollisionLog(PlayerObject* player) {
    player->m_collisionLogTop->removeAllObjects();
    player->m_collisionLogBottom->removeAllObjects();
    player->m_collisionLogLeft->removeAllObjects();
    player->m_collisionLogRight->removeAllObjects();
    player->m_lastCollisionLeft = -1;
    player->m_lastCollisionRight = -1;
    player->m_lastCollisionBottom = -1;
    player->m_lastCollisionTop = -1;
}

void finishIntermediateSegment(PlayLayer* playLayer, PlayerObject* player,
                               float segmentDelta, float fullStepDelta) {
    playLayer->checkCollisions(
        player,
        player->m_isOnSlope && !player->m_isDart ? fullStepDelta : 0.0f,
        true
    );
    player->updateRotation(segmentDelta);
    resetCollisionLog(player);
}
} // namespace

class $modify(SubTickGameLayer, GJBaseGameLayer) {
    struct Fields {
        bool capturedOriginalMode = false;
        bool originalClickBetweenSteps = false;
        bool originalClickOnSteps = false;
    };

    static void onModify(auto& self) {
        (void)self.setHookPriority("GJBaseGameLayer::getModifiedDelta", Priority::VeryEarly);
    }

    void applyConfiguredMode() {
        if (!m_fields->capturedOriginalMode) {
            m_fields->capturedOriginalMode = true;
            m_fields->originalClickBetweenSteps = m_clickBetweenSteps;
            m_fields->originalClickOnSteps = m_clickOnSteps;
        }

        auto const playLayer = PlayLayer::get();
        auto const currentLayer = isCurrentPlayLayer(this, playLayer) ? playLayer : nullptr;

        if (!Runtime::get().isActive()) {
            if (currentLayer && s_engineLayer == currentLayer && s_timelineActive) {
                SubtickEngine::get().flushToVanilla(currentLayer);
            }
            s_timelineActive = false;
            m_clickBetweenSteps = m_fields->originalClickBetweenSteps;
            m_clickOnSteps = m_fields->originalClickOnSteps;
            return;
        }

        if (currentLayer && s_engineLayer != currentLayer) {
            SubtickEngine::get().resetForLayer(currentLayer);
            s_engineLayer = currentLayer;
            s_timelineActive = false;
        }

        // Native input collection stays enabled until the custom frame is validated.
        if (currentLayer && s_engineLayer == currentLayer && s_timelineActive) {
            m_clickBetweenSteps = false;
            m_clickOnSteps = false;
        } else {
            m_clickBetweenSteps = true;
            m_clickOnSteps = false;
        }
    }

    bool init() {
        if (!GJBaseGameLayer::init()) {
            return false;
        }
        applyConfiguredMode();
        return true;
    }

    double getModifiedDelta(float delta) {
        auto const modifiedDelta = GJBaseGameLayer::getModifiedDelta(delta);
        auto const playLayer = PlayLayer::get();

        if (Runtime::get().isActive() && canScheduleSubticks(this, playLayer)) {
            auto const armed = SubtickEngine::get().beginFrame(playLayer, modifiedDelta);
            s_engineLayer = playLayer;
            s_timelineActive = armed;
            if (armed) {
                m_clickBetweenSteps = false;
                m_clickOnSteps = false;
            } else {
                m_clickBetweenSteps = true;
                m_clickOnSteps = false;
            }
        } else {
            if (Runtime::get().isActive() && isCurrentPlayLayer(this, playLayer)) {
                SubtickEngine::get().flushToVanilla(playLayer);
                s_engineLayer = playLayer;
            }
            s_timelineActive = false;
            applyConfiguredMode();
        }

        return modifiedDelta;
    }
};

class $modify(SubTickPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
            return false;
        }

        SubtickEngine::get().resetForLayer(this);
        s_engineLayer = this;
        s_timelineActive = false;
        clearSplitState();
        static_cast<SubTickGameLayer*>(static_cast<GJBaseGameLayer*>(this))
            ->applyConfiguredMode();
        return true;
    }

    void resetLevel() {
        SubtickEngine::get().resetForLayer(this);
        s_engineLayer = this;
        s_timelineActive = false;
        clearSplitState();

        PlayLayer::resetLevel();

        SubtickEngine::get().resetForLayer(this);
        static_cast<SubTickGameLayer*>(static_cast<GJBaseGameLayer*>(this))
            ->applyConfiguredMode();
    }

    void pauseGame(bool unfocused) {
        SubtickEngine::get().flushToVanilla(this);
        s_engineLayer = this;
        s_timelineActive = false;
        clearSplitState();

        PlayLayer::pauseGame(unfocused);
        static_cast<SubTickGameLayer*>(static_cast<GJBaseGameLayer*>(this))
            ->applyConfiguredMode();
    }

    void onQuit() {
        SubtickEngine::get().resetForLayer(nullptr);
        s_engineLayer = nullptr;
        s_timelineActive = false;
        clearSplitState();
        PlayLayer::onQuit();
    }

    void levelComplete() {
        SubtickEngine::get().resetForLayer(nullptr);
        s_engineLayer = nullptr;
        s_timelineActive = false;
        clearSplitState();
        PlayLayer::levelComplete();
    }
};

class $modify(SubTickPlayerObject, PlayerObject) {
    void update(float stepDelta) {
        auto const playLayer = PlayLayer::get();

        if (s_midStep) {
            PlayerObject::update(stepDelta);
            return;
        }

        if (playLayer && this == playLayer->m_player2 && s_skipNextPlayer2Update) {
            s_skipNextPlayer2Update = false;
            return;
        }

        if (!playLayer || this != playLayer->m_player1) {
            PlayerObject::update(stepDelta);
            return;
        }

        s_skipNextPlayer2Update = false;
        s_player1Split = false;
        s_player2Split = false;
        s_player1ShipRotationDeferred = false;
        s_player2ShipRotationDeferred = false;

        auto const mode = Runtime::get().mode();
        if (!Runtime::get().isActive() || s_engineLayer != playLayer || !s_timelineActive) {
            PlayerObject::update(stepDelta);
            return;
        }

        auto& engine = SubtickEngine::get();
        if (!engine.beginTick(playLayer, stepDelta)) {
            s_timelineActive = false;
            PlayerObject::update(stepDelta);
            return;
        }

        auto const inputs = engine.scheduledInputs();
        if (inputs.empty()) {
            PlayerObject::update(stepDelta);
            if (!engine.commitTick()) {
                engine.flushToVanilla(playLayer);
                s_timelineActive = false;
            }
            return;
        }

        auto const player2 = playLayer->m_player2;
        auto const player1StartedOnGround = m_isOnGround;
        auto const player2StartedOnGround = player2 && player2->m_isOnGround;

        s_player1Position = getPosition();
        if (player2) {
            s_player2Position = player2->getPosition();
        }

        s_player1Split =
            shouldSplitPlayer(this, mode) && std::isfinite(stepDelta) && stepDelta > 0.0f;
        s_player2Split =
            player2 && playLayer->m_gameState.m_isDualMode &&
            shouldSplitPlayer(player2, mode) && std::isfinite(stepDelta) && stepDelta > 0.0f;

        auto const splitTick = s_player1Split || s_player2Split;
        float elapsedDelta = 0.0f;
        double previousFraction = 0.0;
        bool firstSegment = true;
        s_fullStepDelta = stepDelta;
        s_midStep = true;

        auto advancePlayer = [this](PlayerObject* player, float segmentDelta) {
            if (!player || segmentDelta <= 0.0f) {
                return;
            }
            if (player == this) {
                PlayerObject::update(segmentDelta);
            } else {
                player->update(segmentDelta);
            }
        };

        std::size_t inputIndex = 0;
        while (inputIndex < inputs.size()) {
            auto const fraction =
                std::max(previousFraction, std::clamp(inputs[inputIndex].fraction, 0.0, 1.0));
            auto const remainingDelta = std::max(0.0f, stepDelta - elapsedDelta);
            auto const requestedDelta =
                static_cast<float>(static_cast<double>(stepDelta) * (fraction - previousFraction));
            auto const segmentDelta = std::clamp(requestedDelta, 0.0f, remainingDelta);

            if (s_player1Split) {
                advancePlayer(this, segmentDelta);
                if (segmentDelta > 0.0f) {
                    if (firstSegment && ((m_yVelocity < 0.0) ^ m_isUpsideDown)) {
                        m_isOnGround = player1StartedOnGround;
                    }
                    finishIntermediateSegment(playLayer, this, segmentDelta, stepDelta);
                }
            }

            if (s_player2Split) {
                advancePlayer(player2, segmentDelta);
                if (segmentDelta > 0.0f) {
                    if (firstSegment && ((player2->m_yVelocity < 0.0) ^ player2->m_isUpsideDown)) {
                        player2->m_isOnGround = player2StartedOnGround;
                    }
                    finishIntermediateSegment(playLayer, player2, segmentDelta, stepDelta);
                }
            }

            elapsedDelta += segmentDelta;
            auto const sameTimestampFraction = inputs[inputIndex].fraction;
            do {
                auto const& input = inputs[inputIndex].input;
                playLayer->handleButton(input.push, input.button, !input.player2);
                engine.recordInputApplied(input.timestamp);
                ++inputIndex;
            } while (inputIndex < inputs.size() &&
                     inputs[inputIndex].fraction == sameTimestampFraction);

            previousFraction = fraction;
            firstSegment = false;
        }

        auto const finalDelta = std::max(0.0f, stepDelta - elapsedDelta);
        s_finalRotationDelta = finalDelta;

        if (s_player1Split) {
            advancePlayer(this, finalDelta);
        } else {
            PlayerObject::update(stepDelta);
        }

        if (player2) {
            if (s_player2Split) {
                advancePlayer(player2, finalDelta);
            } else {
                player2->update(stepDelta);
            }
            s_skipNextPlayer2Update = true;
        }

        s_midStep = false;
        if (engine.commitTick()) {
            if (splitTick) {
                engine.recordSplitTick();
            }
        } else {
            engine.flushToVanilla(playLayer);
            s_timelineActive = false;
        }
    }

    void updateRotation(float delta) {
        auto const playLayer = PlayLayer::get();

        if (s_midStep) {
            PlayerObject::updateRotation(delta);
            return;
        }

        if (playLayer && this == playLayer->m_player1 && s_player1Split) {
            s_replayingShipRotation = true;
            PlayerObject::updateRotation(s_finalRotationDelta);
            s_replayingShipRotation = false;
            m_lastPosition = s_player1Position;
            s_player1Split = false;
            return;
        }

        if (playLayer && this == playLayer->m_player2 && s_player2Split) {
            s_replayingShipRotation = true;
            PlayerObject::updateRotation(s_finalRotationDelta);
            s_replayingShipRotation = false;
            m_lastPosition = s_player2Position;
            s_player2Split = false;
            return;
        }

        PlayerObject::updateRotation(delta);
    }

    void updateShipRotation(float delta) {
        auto const playLayer = PlayLayer::get();

        // Ship interpolation is not associative; defer it and replay the full tick once.
        if (s_midStep && !s_replayingShipRotation && playLayer) {
            if (this == playLayer->m_player1 && s_player1Split) {
                s_player1ShipRotationDeferred = true;
                return;
            }
            if (this == playLayer->m_player2 && s_player2Split) {
                s_player2ShipRotationDeferred = true;
                return;
            }
        }

        if (s_replayingShipRotation && playLayer) {
            if (this == playLayer->m_player1 && s_player1ShipRotationDeferred) {
                s_player1ShipRotationDeferred = false;
                PlayerObject::updateShipRotation(s_fullStepDelta);
                return;
            }
            if (this == playLayer->m_player2 && s_player2ShipRotationDeferred) {
                s_player2ShipRotationDeferred = false;
                PlayerObject::updateShipRotation(s_fullStepDelta);
                return;
            }
        }

        PlayerObject::updateShipRotation(delta);
    }
};
