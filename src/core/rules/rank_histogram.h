#pragma once

#include "../model/card.h"
#include <array>
#include <vector>
#include <map>

namespace fpdz {

// Histogram of card ranks for pattern analysis
class RankHistogram {
public:
    RankHistogram();

    void addCard(Rank rank);
    void addCards(const std::vector<Card>& cards);
    void removeCard(Rank rank);

    int countOf(Rank rank) const;
    int totalCount() const;

    // Get all ranks that have exactly n cards
    std::vector<Rank> ranksWithCount(int n) const;

    // Get all ranks that have at least n cards
    std::vector<Rank> ranksWithAtLeastCount(int n) const;

    // Check if there exists a consecutive sequence of ranks
    // each having at least minCount cards, of given length
    // Only considers ranks that can be in sequences (3-A)
    bool hasConsecutiveSequence(int length, int minCount, Rank& startRank) const;

    // Find all consecutive sequences of given parameters
    std::vector<Rank> findConsecutiveSequences(int length, int minCount) const;

    const std::array<int, RANK_COUNT>& data() const { return m_counts; }

private:
    std::array<int, RANK_COUNT> m_counts{};
};

} // namespace fpdz
