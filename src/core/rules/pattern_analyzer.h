#pragma once

#include "card_pattern.h"
#include "rank_histogram.h"
#include "../model/hand.h"
#include <vector>
#include <optional>

namespace fpdz {

class PatternAnalyzer {
public:
    // Analyze a set of cards and determine if they form a valid pattern
    // Returns Invalid pattern if the cards don't form any valid combination
    static CardPattern analyze(const std::vector<Card>& cards);

    // Analyze by card IDs from a hand
    static CardPattern analyze(const Hand& hand, const std::vector<CardId>& cardIds);

private:
    static std::optional<CardPattern> analyzeSingle(const std::vector<Card>& cards, const RankHistogram& hist);
    static std::optional<CardPattern> analyzePair(const std::vector<Card>& cards, const RankHistogram& hist);
    static std::optional<CardPattern> analyzeTriple(const std::vector<Card>& cards, const RankHistogram& hist);
    static std::optional<CardPattern> analyzeTripleWithSingle(const std::vector<Card>& cards, const RankHistogram& hist);
    static std::optional<CardPattern> analyzeTripleWithPair(const std::vector<Card>& cards, const RankHistogram& hist);
    static std::optional<CardPattern> analyzeStraight(const std::vector<Card>& cards, const RankHistogram& hist);
    static std::optional<CardPattern> analyzeConsecutivePairs(const std::vector<Card>& cards, const RankHistogram& hist);
    static std::optional<CardPattern> analyzeAirplane(const std::vector<Card>& cards, const RankHistogram& hist);
    static std::optional<CardPattern> analyzeAirplaneWithSingles(const std::vector<Card>& cards, const RankHistogram& hist);
    static std::optional<CardPattern> analyzeAirplaneWithPairs(const std::vector<Card>& cards, const RankHistogram& hist);
    static std::optional<CardPattern> analyzeFourWithTwoSingles(const std::vector<Card>& cards, const RankHistogram& hist);
    static std::optional<CardPattern> analyzeFourWithTwoPairs(const std::vector<Card>& cards, const RankHistogram& hist);
    static std::optional<CardPattern> analyzeBombs(const std::vector<Card>& cards, const RankHistogram& hist);
};

} // namespace fpdz
