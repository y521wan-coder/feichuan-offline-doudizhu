#pragma once

#include "ai_level_profile.h"
#include "ai_player.h"
#include "legal_move_generator.h"

namespace fpdz {

class StandardAiPlayer : public AiPlayer {
public:
    explicit StandardAiPlayer(AiDifficulty difficulty = AiDifficulty::Intermediate);

    using AiPlayer::decideBid;
    using AiPlayer::decidePlay;
    AiDifficulty difficulty() const override { return m_profile.level; }
    GameCommand decideBid(const AiObservation& observation) override;
    GameCommand decidePlay(const AiObservation& observation) override;

    int decisionBudgetMilliseconds() const { return m_profile.decisionBudgetMs; }
    const AiLevelProfile& profile() const { return m_profile; }

private:
    AiLevelProfile m_profile;
};

} // namespace fpdz
