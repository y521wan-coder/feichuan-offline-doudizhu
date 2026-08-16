#pragma once

#include "ai_difficulty.h"
#include "../core/engine/game_command.h"
#include "../core/engine/game_state.h"

namespace fpdz {

struct AiObservation {
    PlayerId playerId = PlayerId::Player1;
    GamePhase phase = GamePhase::NotStarted;
    Hand ownHand;
    PublicGameSnapshot publicState;
    int highestBid = 0;
    uint64_t decisionSeed = 0;
};

inline AiObservation makeAiObservation(const GameState& state, PlayerId playerId) {
    AiObservation observation;
    observation.playerId = playerId;
    observation.phase = state.phase();
    observation.ownHand = state.fullState().players[static_cast<int>(playerId)].hand;
    observation.publicState = state.publicSnapshot();
    observation.highestBid = state.fullState().highestBid;
    observation.decisionSeed = state.gameId() * 1000003ULL +
        static_cast<uint64_t>(static_cast<int>(playerId) + 1) * 97ULL +
        state.fullState().eventSequence;
    return observation;
}

class AiPlayer {
public:
    virtual ~AiPlayer() = default;
    virtual AiDifficulty difficulty() const = 0;
    virtual GameCommand decideBid(const AiObservation& observation) = 0;
    virtual GameCommand decidePlay(const AiObservation& observation) = 0;

    GameCommand decideBid(const GameState& state, PlayerId playerId) {
        return decideBid(makeAiObservation(state, playerId));
    }
    GameCommand decidePlay(const GameState& state, PlayerId playerId) {
        return decidePlay(makeAiObservation(state, playerId));
    }
};

} // namespace fpdz
