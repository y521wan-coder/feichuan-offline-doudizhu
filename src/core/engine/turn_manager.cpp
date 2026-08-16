#include "turn_manager.h"

namespace fpdz {

PlayerId TurnManager::advanceTurn(GameState& state) {
    auto& fs = state.fullState();
    fs.currentPlayer = nextPlayer(fs.currentPlayer);
    return fs.currentPlayer;
}

void TurnManager::resetTrick(GameState& state) {
    auto& fs = state.fullState();
    fs.lastPlayedCards.clear();
    fs.consecutivePasses = 0;
    // Current player becomes the leader (the one who played last)
    // This is set by the caller
}

bool TurnManager::isLeader(const GameState& state) {
    const auto& fs = state.fullState();
    return fs.lastPlayedCards.empty();
}

void TurnManager::recordPass(GameState& state) {
    auto& fs = state.fullState();
    fs.consecutivePasses++;
    fs.players[static_cast<int>(fs.currentPlayer)].lastActionWasPass = true;
}

void TurnManager::recordPlay(GameState& state, PlayerId player, const std::vector<Card>& cards) {
    auto& fs = state.fullState();
    fs.lastPlayedCards = cards;
    fs.lastPlayedBy = player;
    fs.consecutivePasses = 0;
    fs.players[static_cast<int>(player)].cardsPlayedCount++;
    fs.players[static_cast<int>(player)].hasPlayedThisRound = true;
    fs.players[static_cast<int>(player)].lastActionWasPass = false;
}

} // namespace fpdz
