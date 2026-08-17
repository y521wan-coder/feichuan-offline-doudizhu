#pragma once

#include "ai_player.h"
#include "legal_move_generator.h"

#include <cstddef>
#include <string>
#include <vector>

namespace fpdz {

struct FarmerTeamDecision {
    bool pass = false;
    std::vector<std::size_t> allowedMoveIndexes;
    std::string reasonCode;
    bool teamRuleException = false;
};

class FarmerTeamStrategy {
public:
    // Enforces the difficulty-independent farmer cooperation rules before any
    // level-specific search or random choice occurs.
    static FarmerTeamDecision selectCandidates(const AiObservation& observation,
                                                const std::vector<LegalMove>& moves,
                                                bool isLeader);

    static int scoreAdjustment(const AiObservation& observation,
                               const LegalMove& move, bool isLeader,
                               Role lastRole);
};

} // namespace fpdz
