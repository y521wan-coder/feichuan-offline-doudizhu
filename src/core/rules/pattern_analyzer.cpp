#include "pattern_analyzer.h"
#include "rule_set.h"
#include <algorithm>
#include <set>

namespace fpdz {

CardPattern PatternAnalyzer::analyze(const std::vector<Card>& cards,
                                     int activePlayerCount) {
    if (cards.empty()) {
        return CardPattern{};
    }

    RankHistogram hist;
    hist.addCards(cards);

    int total = static_cast<int>(cards.size());

    // Try patterns in order of specificity
    // Single
    if (auto p = analyzeSingle(cards, hist)) return *p;
    // Pair
    if (auto p = analyzePair(cards, hist)) return *p;
    // Triple
    if (auto p = analyzeTriple(cards, hist)) return *p;
    // Bombs (check before combinations since they're special)
    if (auto p = analyzeBombs(cards, hist)) return *p;
    const bool standardSingleDeckRules =
        activePlayerCount == TWO_PLAYER_COUNT || activePlayerCount == THREE_PLAYER_COUNT;
    // Standard single-deck Doudizhu allows triple with a single; the existing
    // four-player rules intentionally keep only triple with a pair.
    if (standardSingleDeckRules && total == 4) {
        if (auto p = analyzeTripleWithSingle(cards, hist)) return *p;
    }
    if (total == 5) {
        if (auto p = analyzeTripleWithPair(cards, hist)) return *p;
    }
    // Straight
    if (total >= RuleSet::MIN_STRAIGHT_LENGTH) {
        if (auto p = analyzeStraight(cards, hist)) return *p;
    }
    // Consecutive pairs
    if (total >= RuleSet::MIN_CONSECUTIVE_PAIRS * 2 && total % 2 == 0) {
        if (auto p = analyzeConsecutivePairs(cards, hist)) return *p;
    }
    // Airplane variants
    if (total >= RuleSet::MIN_AIRPLANE_LENGTH * 3) {
        if (auto p = analyzeAirplane(cards, hist)) return *p;
        if (standardSingleDeckRules) {
            if (auto p = analyzeAirplaneWithSingles(cards, hist)) return *p;
        }
        if (auto p = analyzeAirplaneWithPairs(cards, hist, activePlayerCount)) return *p;
    }
    if (standardSingleDeckRules) {
        if (auto p = analyzeFourWithTwoSingles(cards, hist)) return *p;
        if (auto p = analyzeFourWithTwoPairs(cards, hist)) return *p;
    }

    return CardPattern{};
}

CardPattern PatternAnalyzer::analyze(const Hand& hand,
                                     const std::vector<CardId>& cardIds,
                                     int activePlayerCount) {
    std::vector<Card> cards;
    for (CardId id : cardIds) {
        if (!hand.contains(id)) return CardPattern{};
        for (const auto& c : hand.cards()) {
            if (c.id() == id) {
                cards.push_back(c);
                break;
            }
        }
    }
    return analyze(cards, activePlayerCount);
}

std::optional<CardPattern> PatternAnalyzer::analyzeSingle(const std::vector<Card>& cards, const RankHistogram&) {
    if (cards.size() != 1) return std::nullopt;
    CardPattern p;
    p.type = CardPatternType::Single;
    p.mainRank = cards[0].rank();
    p.mainLength = 1;
    p.totalCards = 1;
    p.cards = cards;
    return p;
}

std::optional<CardPattern> PatternAnalyzer::analyzePair(const std::vector<Card>& cards, const RankHistogram& hist) {
    if (cards.size() != 2) return std::nullopt;
    if (cards[0].rank() != cards[1].rank()) return std::nullopt;
    CardPattern p;
    p.type = CardPatternType::Pair;
    p.mainRank = cards[0].rank();
    p.mainLength = 1;
    p.totalCards = 2;
    p.cards = cards;
    return p;
}

std::optional<CardPattern> PatternAnalyzer::analyzeTriple(const std::vector<Card>& cards, const RankHistogram& hist) {
    if (cards.size() != 3) return std::nullopt;
    auto threes = hist.ranksWithCount(3);
    if (threes.size() != 1) return std::nullopt;
    CardPattern p;
    p.type = CardPatternType::Triple;
    p.mainRank = threes[0];
    p.mainLength = 1;
    p.totalCards = 3;
    p.cards = cards;
    return p;
}

std::optional<CardPattern> PatternAnalyzer::analyzeTripleWithSingle(const std::vector<Card>& cards, const RankHistogram& hist) {
    if (cards.size() != 4) return std::nullopt;
    auto threes = hist.ranksWithAtLeastCount(3);
    if (threes.size() != 1) return std::nullopt;
    Rank mainRank = threes[0];
    // Check remaining card is different rank
    int mainCount = 0;
    for (const auto& c : cards) {
        if (c.rank() == mainRank) mainCount++;
    }
    if (mainCount != 3) return std::nullopt;
    CardPattern p;
    p.type = CardPatternType::TripleWithSingle;
    p.mainRank = mainRank;
    p.mainLength = 1;
    p.totalCards = 4;
    p.cards = cards;
    return p;
}

std::optional<CardPattern> PatternAnalyzer::analyzeTripleWithPair(const std::vector<Card>& cards, const RankHistogram& hist) {
    if (cards.size() != 5) return std::nullopt;
    auto threes = hist.ranksWithAtLeastCount(3);
    if (threes.size() != 1) return std::nullopt;
    Rank mainRank = threes[0];
    int mainCount = 0;
    Rank kickerRank = Rank::Three;
    int kickerCount = 0;
    for (const auto& c : cards) {
        if (c.rank() == mainRank) {
            mainCount++;
        } else {
            if (kickerCount == 0) kickerRank = c.rank();
            if (c.rank() == kickerRank) kickerCount++;
        }
    }
    if (mainCount != 3 || kickerCount != 2) return std::nullopt;
    CardPattern p;
    p.type = CardPatternType::TripleWithPair;
    p.mainRank = mainRank;
    p.mainLength = 1;
    p.totalCards = 5;
    p.cards = cards;
    return p;
}

std::optional<CardPattern> PatternAnalyzer::analyzeStraight(const std::vector<Card>& cards, const RankHistogram& hist) {
    int n = static_cast<int>(cards.size());
    if (n < RuleSet::MIN_STRAIGHT_LENGTH) return std::nullopt;

    // Each card must be unique rank, all in sequence range
    auto ones = hist.ranksWithCount(1);
    if (static_cast<int>(ones.size()) != n) return std::nullopt;

    // Check all are sequence-eligible
    for (Rank r : ones) {
        if (!canBeInSequence(r)) return std::nullopt;
    }

    // Sort and check consecutive
    std::vector<int> weights;
    for (Rank r : ones) weights.push_back(rankWeight(r));
    std::sort(weights.begin(), weights.end());

    for (int i = 1; i < n; ++i) {
        if (weights[i] != weights[i-1] + 1) return std::nullopt;
    }

    CardPattern p;
    p.type = CardPatternType::Straight;
    p.mainRank = static_cast<Rank>(weights[0]);
    p.mainLength = n;
    p.totalCards = n;
    p.cards = cards;
    return p;
}

std::optional<CardPattern> PatternAnalyzer::analyzeConsecutivePairs(const std::vector<Card>& cards, const RankHistogram& hist) {
    int n = static_cast<int>(cards.size());
    if (n < RuleSet::MIN_CONSECUTIVE_PAIRS * 2 || n % 2 != 0) return std::nullopt;

    int pairCount = n / 2;
    auto twos = hist.ranksWithCount(2);
    if (static_cast<int>(twos.size()) != pairCount) return std::nullopt;

    // All must be in sequence range
    for (Rank r : twos) {
        if (!canBeInSequence(r)) return std::nullopt;
    }

    std::vector<int> weights;
    for (Rank r : twos) weights.push_back(rankWeight(r));
    std::sort(weights.begin(), weights.end());

    for (int i = 1; i < pairCount; ++i) {
        if (weights[i] != weights[i-1] + 1) return std::nullopt;
    }

    CardPattern p;
    p.type = CardPatternType::ConsecutivePairs;
    p.mainRank = static_cast<Rank>(weights[0]);
    p.mainLength = pairCount;
    p.totalCards = n;
    p.cards = cards;
    return p;
}

std::optional<CardPattern> PatternAnalyzer::analyzeAirplane(const std::vector<Card>& cards, const RankHistogram& hist) {
    int n = static_cast<int>(cards.size());
    if (n < RuleSet::MIN_AIRPLANE_LENGTH * 3 || n % 3 != 0) return std::nullopt;

    int tripleCount = n / 3;
    auto threes = hist.ranksWithCount(3);
    if (static_cast<int>(threes.size()) != tripleCount) return std::nullopt;

    for (Rank r : threes) {
        if (!canBeInSequence(r)) return std::nullopt;
    }

    std::vector<int> weights;
    for (Rank r : threes) weights.push_back(rankWeight(r));
    std::sort(weights.begin(), weights.end());

    for (int i = 1; i < tripleCount; ++i) {
        if (weights[i] != weights[i-1] + 1) return std::nullopt;
    }

    CardPattern p;
    p.type = CardPatternType::Airplane;
    p.mainRank = static_cast<Rank>(weights[0]);
    p.mainLength = tripleCount;
    p.totalCards = n;
    p.cards = cards;
    return p;
}

std::optional<CardPattern> PatternAnalyzer::analyzeAirplaneWithSingles(const std::vector<Card>& cards, const RankHistogram& hist) {
    int n = static_cast<int>(cards.size());
    if (n < RuleSet::MIN_AIRPLANE_LENGTH * 4) return std::nullopt;
    if (n % 4 != 0) return std::nullopt;

    int airplaneLen = n / 4; // each triple + 1 single

    // Find consecutive triples
    auto sequences = hist.findConsecutiveSequences(airplaneLen, 3);
    if (sequences.empty()) return std::nullopt;

    // For each candidate sequence, verify wings don't overlap
    for (Rank startRank : sequences) {
        int startWeight = rankWeight(startRank);
        // Count cards used by airplane body
        int bodyCards = 0;
        for (int i = 0; i < airplaneLen; ++i) {
            bodyCards += hist.countOf(static_cast<Rank>(startWeight + i));
        }
        // Remaining cards are wings
        int wingCards = n - bodyCards;
        if (wingCards != airplaneLen) continue;

        // Check wings don't use airplane body ranks with the same count
        // (wings can use 2 and jokers, and can use other ranks)
        CardPattern p;
        p.type = CardPatternType::AirplaneWithSingles;
        p.mainRank = startRank;
        p.mainLength = airplaneLen;
        p.totalCards = n;
        p.cards = cards;
        return p;
    }
    return std::nullopt;
}

std::optional<CardPattern> PatternAnalyzer::analyzeAirplaneWithPairs(const std::vector<Card>& cards, const RankHistogram& hist, int activePlayerCount) {
    int n = static_cast<int>(cards.size());
    if (n < RuleSet::MIN_AIRPLANE_LENGTH * 5) return std::nullopt;
    if (n % 5 != 0) return std::nullopt;

    int airplaneLen = n / 5;

    auto sequences = hist.findConsecutiveSequences(airplaneLen, 3);
    if (sequences.empty()) return std::nullopt;

    for (Rank startRank : sequences) {
        int startWeight = rankWeight(startRank);
        const int bodyCards = airplaneLen * 3;
        int wingCards = n - bodyCards;
        if (wingCards != airplaneLen * 2) continue;

        // Verify wings form pairs
        // Build a temporary histogram without the body
        RankHistogram tempHist = hist;
        for (int i = 0; i < airplaneLen; ++i) {
            Rank r = static_cast<Rank>(startWeight + i);
            for (int j = 0; j < 3; ++j) {
                tempHist.removeCard(r);
            }
        }
        if (activePlayerCount == PLAYER_COUNT) {
            bool overlapsBody = false;
            for (int i = 0; i < airplaneLen; ++i) {
                if (tempHist.countOf(static_cast<Rank>(startWeight + i)) != 0) {
                    overlapsBody = true;
                    break;
                }
            }
            if (overlapsBody) continue;
        }
        // Remaining should all be pairs
        bool allPairs = true;
        for (int i = 0; i < RANK_COUNT; ++i) {
            int c = tempHist.data()[i];
            if (c != 0 && c != 2) { allPairs = false; break; }
        }
        if (!allPairs) continue;

        CardPattern p;
        p.type = CardPatternType::AirplaneWithPairs;
        p.mainRank = startRank;
        p.mainLength = airplaneLen;
        p.totalCards = n;
        p.cards = cards;
        return p;
    }
    return std::nullopt;
}

std::optional<CardPattern> PatternAnalyzer::analyzeFourWithTwoSingles(const std::vector<Card>& cards, const RankHistogram& hist) {
    if (cards.size() != 6) return std::nullopt;
    auto fours = hist.ranksWithCount(4);
    if (fours.size() != 1) return std::nullopt;

    CardPattern p;
    p.type = CardPatternType::FourWithTwoSingles;
    p.mainRank = fours[0];
    p.mainLength = 1;
    p.totalCards = 6;
    p.cards = cards;
    return p;
}

std::optional<CardPattern> PatternAnalyzer::analyzeFourWithTwoPairs(const std::vector<Card>& cards, const RankHistogram& hist) {
    if (cards.size() != 8) return std::nullopt;
    auto fours = hist.ranksWithAtLeastCount(4);
    if (fours.size() != 1) return std::nullopt;

    Rank mainRank = fours[0];
    // Check remaining 4 cards form 2 pairs
    RankHistogram tempHist = hist;
    for (int i = 0; i < 4; ++i) tempHist.removeCard(mainRank);

    int pairCount = 0;
    bool valid = true;
    for (int i = 0; i < RANK_COUNT; ++i) {
        int c = tempHist.data()[i];
        if (c == 2) pairCount++;
        else if (c != 0) { valid = false; break; }
    }
    if (!valid || pairCount != 2) return std::nullopt;

    CardPattern p;
    p.type = CardPatternType::FourWithTwoPairs;
    p.mainRank = mainRank;
    p.mainLength = 1;
    p.totalCards = 8;
    p.cards = cards;
    return p;
}

std::optional<CardPattern> PatternAnalyzer::analyzeBombs(const std::vector<Card>& cards, const RankHistogram& hist) {
    int n = static_cast<int>(cards.size());

    if (n == 2 && hist.countOf(Rank::SmallJoker) == 1 &&
        hist.countOf(Rank::BigJoker) == 1) {
        CardPattern p;
        p.type = CardPatternType::KingBomb;
        p.mainRank = Rank::BigJoker;
        p.mainLength = 2;
        p.totalCards = 2;
        p.cards = cards;
        return p;
    }

    // Double king bomb: exactly 4 jokers (2 small + 2 big)
    if (n == 4) {
        int smallJokers = hist.countOf(Rank::SmallJoker);
        int bigJokers = hist.countOf(Rank::BigJoker);
        if (smallJokers == 2 && bigJokers == 2) {
            CardPattern p;
            p.type = CardPatternType::HeavenlyLord;
            p.mainRank = Rank::BigJoker;
            p.mainLength = 4;
            p.totalCards = 4;
            p.cards = cards;
            return p;
        }
    }

    // Gun(4), Cannon(5), Rocket(6), Missile(7), SkyBlast(8)
    if (n >= 4 && n <= 8) {
        auto nOfKind = hist.ranksWithCount(n);
        if (nOfKind.size() == 1) {
            Rank r = nOfKind[0];
            if (!canBeInSequence(r) && r != Rank::Two) {
                // Only jokers, which are handled above
                return std::nullopt;
            }
            CardPatternType type;
            switch (n) {
                case 4: type = CardPatternType::Gun; break;
                case 5: type = CardPatternType::Cannon; break;
                case 6: type = CardPatternType::Rocket; break;
                case 7: type = CardPatternType::Missile; break;
                case 8: type = CardPatternType::SkyBlast; break;
                default: return std::nullopt;
            }
            CardPattern p;
            p.type = type;
            p.mainRank = r;
            p.mainLength = n;
            p.totalCards = n;
            p.cards = cards;
            return p;
        }
    }

    return std::nullopt;
}

} // namespace fpdz
