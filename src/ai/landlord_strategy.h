#pragma once

#include "ai_player.h"
#include "legal_move_generator.h"

namespace fpdz {

class LandlordStrategy {
public:
    static int scoreAdjustment(const AiObservation& observation,
                               const LegalMove& move, bool isLeader);
};

} // namespace fpdz
