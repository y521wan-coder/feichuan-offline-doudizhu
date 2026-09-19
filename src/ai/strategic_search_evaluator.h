#pragma once

#include "ai_level_profile.h"
#include "ai_player.h"
#include "legal_move_generator.h"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace fpdz {

// A bounded, deterministic evaluator used after the farmer team-safety filter.
// It only receives AiObservation, so private opponent hands cannot enter either
// the hand planner or the public-information sampling path.
class StrategicSearchEvaluator {
public:
    StrategicSearchEvaluator(const AiObservation& observation,
                             const AiLevelProfile& profile);

    int handPlanAdjustment(const Hand& remainingHand);
    int refinedHandPlanBonus(const Hand& remainingHand);
    int publicInformationAdjustment(const LegalMove& move,
                                    const Hand& remainingHand,
                                    bool isLeader) const;

private:
    using RankCounts = std::array<int, RANK_COUNT>;
    using Sample = std::array<RankCounts, PLAYER_COUNT>;

    int estimateTurns(const Hand& hand, int depth, int& nodesRemaining);
    int fastTurnEstimate(const Hand& hand) const;
    int structuralQuality(const Hand& hand) const;
    std::vector<Sample> buildPublicSamples() const;

    const AiObservation& m_observation;
    const AiLevelProfile& m_profile;
    std::vector<Sample> m_publicSamples;
    std::unordered_map<std::uint64_t, int> m_planCache;
};

} // namespace fpdz
