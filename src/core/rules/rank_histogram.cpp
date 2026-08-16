#include "rank_histogram.h"
#include <algorithm>

namespace fpdz {

RankHistogram::RankHistogram() {
    m_counts.fill(0);
}

void RankHistogram::addCard(Rank rank) {
    int idx = static_cast<int>(rank);
    if (idx >= 0 && idx < RANK_COUNT) {
        m_counts[idx]++;
    }
}

void RankHistogram::addCards(const std::vector<Card>& cards) {
    for (const auto& card : cards) {
        addCard(card.rank());
    }
}

void RankHistogram::removeCard(Rank rank) {
    int idx = static_cast<int>(rank);
    if (idx >= 0 && idx < RANK_COUNT && m_counts[idx] > 0) {
        m_counts[idx]--;
    }
}

int RankHistogram::countOf(Rank rank) const {
    int idx = static_cast<int>(rank);
    if (idx >= 0 && idx < RANK_COUNT) {
        return m_counts[idx];
    }
    return 0;
}

int RankHistogram::totalCount() const {
    int total = 0;
    for (int c : m_counts) total += c;
    return total;
}

std::vector<Rank> RankHistogram::ranksWithCount(int n) const {
    std::vector<Rank> result;
    for (int i = 0; i < RANK_COUNT; ++i) {
        if (m_counts[i] == n) {
            result.push_back(static_cast<Rank>(i));
        }
    }
    return result;
}

std::vector<Rank> RankHistogram::ranksWithAtLeastCount(int n) const {
    std::vector<Rank> result;
    for (int i = 0; i < RANK_COUNT; ++i) {
        if (m_counts[i] >= n) {
            result.push_back(static_cast<Rank>(i));
        }
    }
    return result;
}

bool RankHistogram::hasConsecutiveSequence(int length, int minCount, Rank& startRank) const {
    auto sequences = findConsecutiveSequences(length, minCount);
    if (!sequences.empty()) {
        startRank = sequences[0];
        return true;
    }
    return false;
}

std::vector<Rank> RankHistogram::findConsecutiveSequences(int length, int minCount) const {
    std::vector<Rank> result;

    // Only consider ranks that can be in sequences (3 through A)
    int maxSeqRank = static_cast<int>(Rank::Ace); // index 11

    for (int start = 0; start <= maxSeqRank - length + 1; ++start) {
        bool valid = true;
        for (int j = 0; j < length; ++j) {
            if (m_counts[start + j] < minCount) {
                valid = false;
                break;
            }
        }
        if (valid) {
            result.push_back(static_cast<Rank>(start));
        }
    }
    return result;
}

} // namespace fpdz
