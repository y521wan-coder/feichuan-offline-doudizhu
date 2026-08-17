#include "bidding_strategy.h"

#include "../core/model/deck.h"
#include "../core/rules/rank_histogram.h"

#include <algorithm>
#include <array>
#include <random>
#include <vector>

namespace fpdz {
namespace {

int longestRun(const RankHistogram& histogram, int minimumCount) {
    int longest = 0;
    int current = 0;
    for (int value = static_cast<int>(Rank::Three);
         value <= static_cast<int>(Rank::Ace); ++value) {
        if (histogram.countOf(static_cast<Rank>(value)) >= minimumCount) {
            longest = std::max(longest, ++current);
        } else {
            current = 0;
        }
    }
    return longest;
}

int structuralScore(const std::vector<Card>& cards) {
    RankHistogram histogram;
    histogram.addCards(cards);

    int score = 0;
    int groups = 0;
    int singletons = 0;
    for (int value = 0; value < RANK_COUNT; ++value) {
        const int count = histogram.countOf(static_cast<Rank>(value));
        if (count == 0) continue;
        ++groups;
        if (count == 1) ++singletons;
        if (count >= 4) score += 4 + (count - 4) * 3;
    }

    const int smallJokers = histogram.countOf(Rank::SmallJoker);
    const int bigJokers = histogram.countOf(Rank::BigJoker);
    score += histogram.countOf(Rank::Two) * 2;
    score += smallJokers * 2 + bigJokers * 3;
    if (smallJokers >= 1 && bigJokers >= 1) score += 4;
    if (smallJokers == 2 && bigJokers == 2) score += 10;

    const int straightRun = longestRun(histogram, 1);
    const int pairRun = longestRun(histogram, 2);
    const int tripleRun = longestRun(histogram, 3);
    if (straightRun >= 5) score += 2 + (straightRun - 5);
    if (pairRun >= 3) score += 3 + (pairRun - 3) * 2;
    if (tripleRun >= 2) score += 5 + (tripleRun - 2) * 3;

    // Fewer rank groups and fewer isolated cards generally mean fewer turns.
    score += std::max(0, 12 - groups) / 2;
    score -= std::max(0, singletons - 6);
    return score;
}

double expectedBottomImprovement(const Hand& hand, const AiLevelProfile& profile,
                                 uint64_t seed) {
    if (profile.publicInferenceSamples <= 0) return 0.0;
    const int base = structuralScore(hand.cards());
    auto unknown = Deck::createDoubleDeck();
    unknown.erase(std::remove_if(unknown.begin(), unknown.end(), [&](const Card& card) {
        return hand.contains(card.id());
    }), unknown.end());

    std::mt19937_64 random(seed ^ 0x424F54544F4D5F38ULL);
    double total = 0.0;
    std::vector<Card> hypothetical = hand.cards();
    hypothetical.reserve(hand.size() + BOTTOM_CARDS);
    for (int sample = 0; sample < profile.publicInferenceSamples; ++sample) {
        std::shuffle(unknown.begin(), unknown.end(), random);
        hypothetical.resize(hand.size());
        hypothetical.insert(hypothetical.end(), unknown.begin(),
                            unknown.begin() + BOTTOM_CARDS);
        total += structuralScore(hypothetical) - base;
    }
    return total / profile.publicInferenceSamples;
}

} // namespace

int BiddingStrategy::decideBid(const Hand& hand, int currentHighestBid,
                               AiDifficulty difficulty, uint64_t randomSalt) {
    const auto& profile = aiLevelProfile(difficulty);
    double score = structuralScore(hand.cards());
    if (difficulty == AiDifficulty::Advanced) {
        score += expectedBottomImprovement(hand, profile, randomSalt) * 0.45;
    }

    int conservativeOffset = 0;
    int passChance = 0;
    switch (difficulty) {
    case AiDifficulty::Beginner:
        conservativeOffset = 3;
        passChance = 24;
        break;
    case AiDifficulty::Intermediate:
        conservativeOffset = 1;
        passChance = 8;
        break;
    case AiDifficulty::Advanced:
        passChance = 1;
        break;
    }

    int bid = 0;
    if (score >= 24 + conservativeOffset) bid = 3;
    else if (score >= 17 + conservativeOffset) bid = 2;
    else if (score >= 11 + conservativeOffset) bid = 1;
    if (bid <= currentHighestBid) return 0;

    std::mt19937_64 random(randomSalt ^ 0x4249445F53414645ULL);
    std::uniform_int_distribution<int> roll(0, 99);
    if (roll(random) < passChance) return 0;
    return std::clamp(bid, currentHighestBid + 1, 3);
}

} // namespace fpdz
