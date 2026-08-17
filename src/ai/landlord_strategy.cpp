#include "landlord_strategy.h"

#include <algorithm>

namespace fpdz {

int LandlordStrategy::scoreAdjustment(const AiObservation& observation,
                                      const LegalMove& move, bool isLeader) {
    int closestFarmer = 99;
    for (const auto& player : observation.publicState.players) {
        if (player.role == Role::Farmer) {
            closestFarmer = std::min(closestFarmer, player.remainingCards);
        }
    }
    int adjustment = 0;
    if (closestFarmer <= 2 && !isLeader) {
        adjustment += static_cast<int>(move.cards.size()) * 180;
    }
    if (move.pattern.isBomb() && closestFarmer > 2) adjustment -= 650;
    return adjustment;
}

} // namespace fpdz
