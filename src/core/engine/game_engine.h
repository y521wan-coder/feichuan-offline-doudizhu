#pragma once

#include "game_state.h"
#include "game_command.h"
#include "turn_manager.h"
#include "../model/deck.h"
#include "../rules/pattern_analyzer.h"
#include "../rules/pattern_comparator.h"
#include "../rules/scoring_engine.h"
#include <vector>
#include <functional>

namespace fpdz {

class GameEngine {
public:
    GameEngine();

    // Execute a command and return the result
    CommandResult execute(const GameCommand& command);

    // Get current state
    const GameState& state() const { return m_state; }
    const FullGameState& fullState() const { return m_state.fullState(); }
    GameState& state() { return m_state; }

    // Get public snapshot
    PublicGameSnapshot publicSnapshot() const { return m_state.publicSnapshot(); }

private:
    CommandResult handleStartGame(const GameCommand& cmd);
    CommandResult handleBid(const GameCommand& cmd);
    CommandResult handlePlayCards(const GameCommand& cmd);
    CommandResult handlePass(const GameCommand& cmd);
    CommandResult handlePause(const GameCommand& cmd);
    CommandResult handleResume(const GameCommand& cmd);
    CommandResult handleAbandon(const GameCommand& cmd);
    CommandResult handleNextRound(const GameCommand& cmd);

    // Internal helpers
    void dealCards(const std::vector<Card>& deck);
    GameEvent createEvent(GameEventType type);
    void applyBombMultiplier(CardPatternType bombType);

    GameState m_state;
    uint64_t m_gameIdCounter = 1;
};

} // namespace fpdz

