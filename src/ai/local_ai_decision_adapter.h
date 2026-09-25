#pragma once

#include "ai_difficulty.h"
#include "ai_player.h"

namespace fpdz {

// Narrow synchronous boundary used by offline mode and local seats in AI battle.
// It intentionally delegates without transforming the observation or command so
// the frozen 2.1 StandardAiPlayer behavior remains byte-for-byte comparable.
class LocalAiDecisionAdapter {
public:
    GameCommand requestBid(const AiObservation& observation,
                           AiDifficulty difficulty) const;
    GameCommand requestPlay(const AiObservation& observation,
                            AiDifficulty difficulty) const;
};

} // namespace fpdz
