#pragma once

#include "ai_player.h"
#include "heuristic_model.h"
#include "legal_move_generator.h"

namespace fpdz {

class StandardAiPlayer : public AiPlayer {
public:
    explicit StandardAiPlayer(AiDifficulty difficulty = AiDifficulty::Intermediate);
    StandardAiPlayer(AiDifficulty difficulty, const HeuristicWeights& weights);

    using AiPlayer::decideBid;
    using AiPlayer::decidePlay;
    AiDifficulty difficulty() const override { return m_difficulty; }
    GameCommand decideBid(const AiObservation& observation) override;
    GameCommand decidePlay(const AiObservation& observation) override;

    int decisionBudgetMilliseconds() const;

private:
    AiDifficulty m_difficulty = AiDifficulty::Intermediate;
    HeuristicWeights m_weights;
};

} // namespace fpdz
