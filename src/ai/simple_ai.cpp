#include "simple_ai.h"

#include "standard_ai.h"

namespace fpdz {

SimpleAiPlayer::SimpleAiPlayer(AiDifficulty difficulty)
    : m_difficulty(difficulty) {}

GameCommand SimpleAiPlayer::decideBid(const AiObservation& observation) {
    return StandardAiPlayer(m_difficulty).decideBid(observation);
}

GameCommand SimpleAiPlayer::decidePlay(const AiObservation& observation) {
    return StandardAiPlayer(m_difficulty).decidePlay(observation);
}

} // namespace fpdz
