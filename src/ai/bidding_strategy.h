#pragma once

#include "ai_difficulty.h"
#include "../core/model/hand.h"
#include "../core/model/card.h"
#include "heuristic_model.h"
#include <cstdint>
#include <vector>

namespace fpdz {

class BiddingStrategy {
public:
    static int decideBid(const Hand& hand, const std::vector<Card>& publicBottomCards,
                         int currentHighestBid,
                         AiDifficulty difficulty = AiDifficulty::Beginner,
                         uint64_t randomSalt = 0,
                         const HeuristicWeights& weights = HeuristicWeights::defaults());
};

} // namespace fpdz
