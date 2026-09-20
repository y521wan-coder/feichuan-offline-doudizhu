#pragma once

#include "ai_difficulty.h"
#include "ai_level_profile.h"
#include "../core/model/hand.h"

#include <cstdint>

namespace fpdz {

class BiddingStrategy {
public:
    // Bidding is intentionally based on the player's own hand only.
    // Advanced probability sampling creates hypothetical bottoms from the
    // unknown pool and never receives the actual hidden bottom cards.
    static int decideBid(const Hand& hand, int currentHighestBid,
                         AiDifficulty difficulty = AiDifficulty::Beginner,
                         uint64_t randomSalt = 0,
                         int activePlayerCount = PLAYER_COUNT);
};

} // namespace fpdz
