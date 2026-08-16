#pragma once

#include "game_state.h"
#include "../model/player.h"

namespace fpdz {

class TurnManager {
public:
    // Advance to next player
    static PlayerId advanceTurn(GameState& state);

    // Reset trick (all others passed)
    static void resetTrick(GameState& state);

    // Check if current player is the leader (free to play anything)
    static bool isLeader(const GameState& state);

    // Record a pass
    static void recordPass(GameState& state);

    // Record a play
    static void recordPlay(GameState& state, PlayerId player, const std::vector<Card>& cards);
};

} // namespace fpdz
