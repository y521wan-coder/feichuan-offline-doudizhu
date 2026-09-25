#include "local_ai_decision_adapter.h"

#include "standard_ai.h"

namespace fpdz {

GameCommand LocalAiDecisionAdapter::requestBid(const AiObservation& observation,
                                               AiDifficulty difficulty) const {
    StandardAiPlayer player(difficulty);
    return player.decideBid(observation);
}

GameCommand LocalAiDecisionAdapter::requestPlay(const AiObservation& observation,
                                                AiDifficulty difficulty) const {
    StandardAiPlayer player(difficulty);
    return player.decidePlay(observation);
}

} // namespace fpdz
