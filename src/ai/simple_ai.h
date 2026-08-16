#pragma once

#include "ai_player.h"

namespace fpdz {

class SimpleAiPlayer : public AiPlayer {
public:
    explicit SimpleAiPlayer(AiDifficulty difficulty = AiDifficulty::Beginner);

    using AiPlayer::decideBid;
    using AiPlayer::decidePlay;
    AiDifficulty difficulty() const override { return m_difficulty; }
    GameCommand decideBid(const AiObservation& observation) override;
    GameCommand decidePlay(const AiObservation& observation) override;

private:
    AiDifficulty m_difficulty = AiDifficulty::Beginner;
};

} // namespace fpdz
