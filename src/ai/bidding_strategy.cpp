#include "bidding_strategy.h"
#include "../core/rules/rank_histogram.h"
#include <algorithm>
#include <random>

namespace fpdz {

int BiddingStrategy::decideBid(const Hand& hand, const std::vector<Card>& publicBottomCards,
                               int currentHighestBid,
                               AiDifficulty difficulty, uint64_t randomSalt,
                               const HeuristicWeights& weights) {
    RankHistogram hist;
    hist.addCards(hand.cards());
    // The eight bottom cards are public before bidding. If this player becomes
    // landlord they join the hand, so bidding must evaluate the combined shape.
    hist.addCards(publicBottomCards);

    int score = 0;
    for (int count = 4; count <= 8; ++count) {
        auto bombs = hist.ranksWithCount(count);
        score += static_cast<int>(bombs.size()) * weights.bidBombWeight;
    }
    score += hist.countOf(Rank::BigJoker) * weights.bidBigJokerWeight;
    score += hist.countOf(Rank::SmallJoker) * weights.bidSmallJokerWeight;
    score += hist.countOf(Rank::Two) * weights.bidTwoWeight;

    int baseBid = 0;
    if (score >= weights.bidThresholdThree) baseBid = 3;
    else if (score >= weights.bidThresholdTwo) baseBid = 2;
    else if (score >= weights.bidThresholdOne) baseBid = 1;

    std::mt19937 rng(static_cast<uint32_t>(
        randomSalt ^ (static_cast<uint64_t>(score) << 16) ^ 0x9E3779B97F4A7C15ULL));
    std::uniform_int_distribution<int> rollDist(0, 99);
    const int roll = rollDist(rng);

    int passChance = 20;
    int bluffChance = 12;
    switch (difficulty) {
    case AiDifficulty::Beginner:
        passChance = 30;
        bluffChance = 18;
        break;
    case AiDifficulty::Intermediate:
        passChance = 20;
        bluffChance = 10;
        break;
    case AiDifficulty::Master:
        passChance = 5;
        bluffChance = 2;
        break;
    }
    passChance = std::clamp(passChance + weights.bidPassAdjustment, 0, 95);

    if (baseBid <= currentHighestBid) {
        if (currentHighestBid == 0 && roll < bluffChance) {
            return 1;
        }
        return 0;
    }

    if (roll < passChance) {
        return 0;
    }

    const int minimumBid = currentHighestBid + 1;
    const int maximumBid = std::clamp(baseBid, minimumBid, 3);
    if (maximumBid <= minimumBid) {
        return minimumBid;
    }

    std::uniform_int_distribution<int> bidDist(minimumBid, maximumBid);
    return bidDist(rng);
}

} // namespace fpdz
