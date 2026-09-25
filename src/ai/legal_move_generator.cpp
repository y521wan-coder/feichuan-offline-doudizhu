#include "legal_move_generator.h"
#include "../core/rules/pattern_analyzer.h"
#include "../core/rules/pattern_comparator.h"
#include <algorithm>
#include <set>

namespace fpdz {
namespace {

using Groups = std::map<Rank, std::vector<Card>>;

std::vector<Card> takeCards(const Groups& groups, Rank rank, int count, int offset = 0) {
    const auto it = groups.find(rank);
    if (it == groups.end() || static_cast<int>(it->second.size()) < offset + count) return {};
    return {it->second.begin() + offset, it->second.begin() + offset + count};
}

void appendCards(std::vector<Card>& target, const std::vector<Card>& cards) {
    target.insert(target.end(), cards.begin(), cards.end());
}

void chooseRanks(const std::vector<Rank>& ranks, int needed, size_t index,
                 std::vector<Rank>& current, std::vector<std::vector<Rank>>& output) {
    if (static_cast<int>(current.size()) == needed) {
        output.push_back(current);
        return;
    }
    for (size_t i = index; i < ranks.size(); ++i) {
        current.push_back(ranks[i]);
        chooseRanks(ranks, needed, i + 1, current, output);
        current.pop_back();
    }
}

void chooseCards(const std::vector<Card>& cards, int needed, size_t index,
                 std::vector<Card>& current, std::vector<std::vector<Card>>& output) {
    if (static_cast<int>(current.size()) == needed) {
        output.push_back(current);
        return;
    }
    const int stillNeeded = needed - static_cast<int>(current.size());
    for (size_t i = index; i + static_cast<size_t>(stillNeeded) <= cards.size(); ++i) {
        current.push_back(cards[i]);
        chooseCards(cards, needed, i + 1, current, output);
        current.pop_back();
    }
}

void addCandidate(std::vector<LegalMove>& moves, std::set<std::vector<CardId>>& seen,
                  std::vector<Card> cards, int activePlayerCount) {
    const auto pattern = PatternAnalyzer::analyze(cards, activePlayerCount);
    if (!pattern.isValid()) return;
    std::vector<CardId> ids;
    ids.reserve(cards.size());
    for (const auto& card : cards) ids.push_back(card.id());
    std::sort(ids.begin(), ids.end());
    if (!seen.insert(ids).second) return;
    moves.push_back({std::move(cards), pattern});
}

void addSequences(std::vector<LegalMove>& moves, std::set<std::vector<CardId>>& seen,
                  const Groups& groups, int copies, int minimumLength,
                  int activePlayerCount) {
    const int first = static_cast<int>(Rank::Three);
    const int last = static_cast<int>(Rank::Ace);
    for (int start = first; start <= last; ++start) {
        for (int end = start + minimumLength - 1; end <= last; ++end) {
            bool valid = true;
            std::vector<Card> cards;
            for (int value = start; value <= end; ++value) {
                auto selected = takeCards(groups, static_cast<Rank>(value), copies);
                if (selected.empty()) {
                    valid = false;
                    break;
                }
                appendCards(cards, selected);
            }
            if (valid) addCandidate(moves, seen, std::move(cards), activePlayerCount);
        }
    }
}

} // namespace

std::vector<LegalMove> LegalMoveGenerator::generateLegalMoves(
    const Hand& hand, const std::optional<CardPattern>& lastPlay,
    int activePlayerCount) {
    std::vector<LegalMove> allMoves;
    std::set<std::vector<CardId>> seen;
    const auto groups = hand.groupByRank();

    for (const auto& [rank, cards] : groups) {
        (void)rank;
        addCandidate(allMoves, seen, {cards.front()}, activePlayerCount);
        if (cards.size() >= 2) {
            addCandidate(allMoves, seen,
                         std::vector<Card>(cards.begin(), cards.begin() + 2),
                         activePlayerCount);
        }
        if (cards.size() >= 3) {
            addCandidate(allMoves, seen,
                         std::vector<Card>(cards.begin(), cards.begin() + 3),
                         activePlayerCount);
        }
        for (int count = 4; count <= static_cast<int>(cards.size()) && count <= 8; ++count) {
            addCandidate(allMoves, seen,
                         std::vector<Card>(cards.begin(), cards.begin() + count),
                         activePlayerCount);
        }
    }

    std::vector<Rank> pairRanks;
    std::vector<Rank> tripleRanks;
    for (const auto& [rank, cards] : groups) {
        if (cards.size() >= 2) pairRanks.push_back(rank);
        if (cards.size() >= 3) tripleRanks.push_back(rank);
    }

    for (const auto tripleRank : tripleRanks) {
        if (activePlayerCount == TWO_PLAYER_COUNT ||
            activePlayerCount == THREE_PLAYER_COUNT) {
            for (const auto& [singleRank, singleCards] : groups) {
                if (singleRank == tripleRank) continue;
                auto cards = takeCards(groups, tripleRank, 3);
                cards.push_back(singleCards.front());
                addCandidate(allMoves, seen, std::move(cards), activePlayerCount);
            }
        }
        for (const auto pairRank : pairRanks) {
            if (pairRank == tripleRank) continue;
            auto cards = takeCards(groups, tripleRank, 3);
            appendCards(cards, takeCards(groups, pairRank, 2));
            addCandidate(allMoves, seen, std::move(cards), activePlayerCount);
        }
    }

    addSequences(allMoves, seen, groups, 1, 5, activePlayerCount);
    addSequences(allMoves, seen, groups, 2, 3, activePlayerCount);
    addSequences(allMoves, seen, groups, 3, 2, activePlayerCount);

    const int first = static_cast<int>(Rank::Three);
    const int last = static_cast<int>(Rank::Ace);
    for (int start = first; start <= last; ++start) {
        for (int length = 2; start + length - 1 <= last; ++length) {
            std::vector<Card> body;
            bool validBody = true;
            std::set<Rank> bodyRanks;
            for (int offset = 0; offset < length; ++offset) {
                const auto rank = static_cast<Rank>(start + offset);
                auto triple = takeCards(groups, rank, 3);
                if (triple.empty()) {
                    validBody = false;
                    break;
                }
                bodyRanks.insert(rank);
                appendCards(body, triple);
            }
            if (!validBody) break;

            if (activePlayerCount == TWO_PLAYER_COUNT ||
                activePlayerCount == THREE_PLAYER_COUNT) {
                std::vector<Card> singleWings;
                for (const auto& [rank, cards] : groups) {
                    if (!bodyRanks.contains(rank)) appendCards(singleWings, cards);
                }
                std::vector<std::vector<Card>> singleChoices;
                std::vector<Card> currentCards;
                chooseCards(singleWings, length, 0, currentCards, singleChoices);
                for (const auto& choice : singleChoices) {
                    auto cards = body;
                    appendCards(cards, choice);
                    addCandidate(allMoves, seen, std::move(cards), activePlayerCount);
                }
            }

            std::vector<Rank> wingRanks;
            for (const auto rank : pairRanks) {
                if (activePlayerCount == PLAYER_COUNT && bodyRanks.contains(rank)) continue;
                const int requiredCards = bodyRanks.contains(rank) ? 5 : 2;
                if (static_cast<int>(groups.at(rank).size()) >= requiredCards) {
                    wingRanks.push_back(rank);
                }
            }
            if (static_cast<int>(wingRanks.size()) < length) continue;
            std::vector<std::vector<Rank>> choices;
            std::vector<Rank> current;
            chooseRanks(wingRanks, length, 0, current, choices);
            for (const auto& choice : choices) {
                auto cards = body;
                for (const auto rank : choice) {
                    appendCards(cards, takeCards(groups, rank, 2,
                        bodyRanks.contains(rank) ? 3 : 0));
                }
                addCandidate(allMoves, seen, std::move(cards), activePlayerCount);
            }
        }
    }

    if (activePlayerCount == TWO_PLAYER_COUNT ||
        activePlayerCount == THREE_PLAYER_COUNT) {
        for (const auto& [fourRank, fourCards] : groups) {
            if (fourCards.size() < 4) continue;
            std::vector<Card> wings;
            for (const auto& [rank, cards] : groups) {
                if (rank != fourRank) appendCards(wings, cards);
            }
            std::vector<std::vector<Card>> singleChoices;
            std::vector<Card> currentCards;
            chooseCards(wings, 2, 0, currentCards, singleChoices);
            for (const auto& choice : singleChoices) {
                auto cards = takeCards(groups, fourRank, 4);
                appendCards(cards, choice);
                addCandidate(allMoves, seen, std::move(cards), activePlayerCount);
            }

            std::vector<Rank> availablePairs;
            for (const auto rank : pairRanks) {
                if (rank != fourRank) availablePairs.push_back(rank);
            }
            std::vector<std::vector<Rank>> pairChoices;
            std::vector<Rank> currentRanks;
            chooseRanks(availablePairs, 2, 0, currentRanks, pairChoices);
            for (const auto& choice : pairChoices) {
                auto cards = takeCards(groups, fourRank, 4);
                for (const auto rank : choice) {
                    appendCards(cards, takeCards(groups, rank, 2));
                }
                addCandidate(allMoves, seen, std::move(cards), activePlayerCount);
            }
        }
    }

    const auto oneSmallJoker = takeCards(groups, Rank::SmallJoker, 1);
    const auto oneBigJoker = takeCards(groups, Rank::BigJoker, 1);
    if (!oneSmallJoker.empty() && !oneBigJoker.empty()) {
        addCandidate(allMoves, seen, {oneSmallJoker.front(), oneBigJoker.front()},
                     activePlayerCount);
    }
    const auto smallJokers = takeCards(groups, Rank::SmallJoker, 2);
    const auto bigJokers = takeCards(groups, Rank::BigJoker, 2);
    if (smallJokers.size() == 2 && bigJokers.size() == 2) {
        auto cards = smallJokers;
        appendCards(cards, bigJokers);
        addCandidate(allMoves, seen, std::move(cards), activePlayerCount);
    }

    if (lastPlay.has_value()) {
        std::erase_if(allMoves, [&](const LegalMove& move) {
            return !PatternComparator::canBeat(move.pattern, *lastPlay);
        });
    }

    std::sort(allMoves.begin(), allMoves.end(), [](const LegalMove& left, const LegalMove& right) {
        if (left.cards.size() != right.cards.size()) return left.cards.size() > right.cards.size();
        if (left.pattern.isBomb() != right.pattern.isBomb()) return !left.pattern.isBomb();
        if (left.pattern.type != right.pattern.type) return left.pattern.type < right.pattern.type;
        return rankWeight(left.pattern.mainRank) < rankWeight(right.pattern.mainRank);
    });
    return allMoves;
}

std::vector<std::vector<Card>> LegalMoveGenerator::generateFreePlayMoves(
    const Hand& hand, int activePlayerCount) {
    std::vector<std::vector<Card>> moves;
    for (auto& move : generateLegalMoves(hand, std::nullopt, activePlayerCount)) {
        moves.push_back(std::move(move.cards));
    }
    return moves;
}

std::vector<std::vector<Card>> LegalMoveGenerator::generateResponseMoves(
    const Hand& hand, const CardPattern& lastPlay, int activePlayerCount) {
    std::vector<std::vector<Card>> moves;
    for (auto& move : generateLegalMoves(hand, lastPlay, activePlayerCount)) {
        moves.push_back(std::move(move.cards));
    }
    return moves;
}

} // namespace fpdz
