#include "strategic_search_evaluator.h"

#include "../core/rules/pattern_comparator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>

namespace fpdz {
namespace {

constexpr std::uint64_t kSamplingSalt = 0x5055424C49435F41ULL;
using RankCounts = std::array<int, RANK_COUNT>;

std::uint64_t handKey(const Hand& hand, int depth) {
    std::uint64_t key = static_cast<std::uint64_t>(depth & 0x7);
    for (int rank = 0; rank < RANK_COUNT; ++rank) {
        key = key * 9 + static_cast<std::uint64_t>(
            hand.countOfRank(static_cast<Rank>(rank)));
    }
    return key;
}

std::vector<CardId> idsOf(const LegalMove& move) {
    std::vector<CardId> ids;
    ids.reserve(move.cards.size());
    for (const auto& card : move.cards) ids.push_back(card.id());
    return ids;
}

int looseCardCount(const Hand& hand) {
    int loose = 0;
    const auto groups = hand.groupByRank();
    for (const auto& [rank, cards] : groups) {
        if (cards.size() == 1 && rank >= Rank::Two) {
            ++loose;
            continue;
        }
        if (cards.size() != 1 || !canBeInSequence(rank)) continue;
        const int value = rankWeight(rank);
        const bool hasLeft = value > rankWeight(Rank::Three) &&
            hand.countOfRank(static_cast<Rank>(value - 1)) > 0;
        const bool hasRight = value < rankWeight(Rank::Ace) &&
            hand.countOfRank(static_cast<Rank>(value + 1)) > 0;
        if (!hasLeft && !hasRight) ++loose;
    }
    return loose;
}

int sequenceCoverage(const Hand& hand, int copies) {
    int best = 0;
    int current = 0;
    for (int value = rankWeight(Rank::Three); value <= rankWeight(Rank::Ace); ++value) {
        if (hand.countOfRank(static_cast<Rank>(value)) >= copies) {
            ++current;
            best = std::max(best, current);
        } else {
            current = 0;
        }
    }
    return best * copies;
}

int bombCount(const Hand& hand) {
    int bombs = 0;
    for (const auto& [rank, cards] : hand.groupByRank()) {
        (void)rank;
        if (cards.size() >= 4) ++bombs;
    }
    if (hand.countOfRank(Rank::SmallJoker) >= 1 &&
        hand.countOfRank(Rank::BigJoker) >= 1) {
        ++bombs;
    }
    return bombs;
}

int typePriority(const LegalMove& move) {
    int result = static_cast<int>(move.cards.size()) * 120;
    if (move.pattern.isBomb()) result -= 480;
    result -= rankWeight(move.pattern.mainRank) * 3;
    switch (move.pattern.type) {
        case CardPatternType::Straight:
        case CardPatternType::ConsecutivePairs:
        case CardPatternType::Airplane:
        case CardPatternType::AirplaneWithSingles:
        case CardPatternType::AirplaneWithPairs:
        case CardPatternType::FourWithTwoSingles:
        case CardPatternType::FourWithTwoPairs:
            result += 130;
            break;
        case CardPatternType::TripleWithSingle:
        case CardPatternType::TripleWithPair:
            result += 70;
            break;
        default:
            break;
    }
    return result;
}

bool hasSequence(const RankCounts& counts,
                 int copies, int length, int firstGreaterThan) {
    const int lastStart = rankWeight(Rank::Ace) - length + 1;
    for (int start = firstGreaterThan + 1; start <= lastStart; ++start) {
        bool available = true;
        for (int offset = 0; offset < length; ++offset) {
            if (counts[start + offset] < copies) {
                available = false;
                break;
            }
        }
        if (available) return true;
    }
    return false;
}

int highestBombLevel(const RankCounts& counts,
                     int* sameLevelBestRank = nullptr) {
    int highestLevel = 0;
    int bestRank = -1;
    for (int rank = 0; rank < RANK_COUNT; ++rank) {
        const int count = counts[rank];
        const int level = count >= 8 ? 6 : count >= 7 ? 5 : count >= 6 ? 4
            : count >= 5 ? 3 : count >= 4 ? 1 : 0;
        if (level > highestLevel || (level == highestLevel && rank > bestRank)) {
            highestLevel = level;
            bestRank = rank;
        }
    }
    const bool kingBomb = counts[static_cast<int>(Rank::SmallJoker)] >= 1 &&
        counts[static_cast<int>(Rank::BigJoker)] >= 1;
    if (kingBomb && highestLevel <= 2) {
        highestLevel = 2;
        bestRank = rankWeight(Rank::BigJoker);
    }
    const bool heavenlyLord = counts[static_cast<int>(Rank::SmallJoker)] >= 2 &&
        counts[static_cast<int>(Rank::BigJoker)] >= 2;
    if (heavenlyLord) {
        highestLevel = 7;
        bestRank = rankWeight(Rank::BigJoker);
    }
    if (sameLevelBestRank) *sameLevelBestRank = bestRank;
    return highestLevel;
}

bool hasSamePatternResponse(const RankCounts& counts,
                            const CardPattern& pattern) {
    const int main = rankWeight(pattern.mainRank);
    switch (pattern.type) {
        case CardPatternType::Single:
            for (int rank = main + 1; rank < RANK_COUNT; ++rank) {
                if (counts[rank] >= 1) return true;
            }
            return false;
        case CardPatternType::Pair:
            for (int rank = main + 1; rank < RANK_COUNT; ++rank) {
                if (counts[rank] >= 2) return true;
            }
            return false;
        case CardPatternType::Triple:
            for (int rank = main + 1; rank < RANK_COUNT; ++rank) {
                if (counts[rank] >= 3) return true;
            }
            return false;
        case CardPatternType::TripleWithSingle:
            for (int triple = main + 1; triple < RANK_COUNT; ++triple) {
                if (counts[triple] < 3) continue;
                for (int single = 0; single < RANK_COUNT; ++single) {
                    if (single != triple && counts[single] >= 1) return true;
                }
            }
            return false;
        case CardPatternType::TripleWithPair:
            for (int triple = main + 1; triple < RANK_COUNT; ++triple) {
                if (counts[triple] < 3) continue;
                for (int pair = 0; pair < RANK_COUNT; ++pair) {
                    if (pair != triple && counts[pair] >= 2) return true;
                }
            }
            return false;
        case CardPatternType::Straight:
            return hasSequence(counts, 1, pattern.mainLength, main);
        case CardPatternType::ConsecutivePairs:
            return hasSequence(counts, 2, pattern.mainLength, main);
        case CardPatternType::Airplane:
            return hasSequence(counts, 3, pattern.mainLength, main);
        case CardPatternType::AirplaneWithSingles:
            for (int start = main + 1;
                 start <= rankWeight(Rank::Ace) - pattern.mainLength + 1; ++start) {
                RankCounts remaining = counts;
                bool body = true;
                for (int offset = 0; offset < pattern.mainLength; ++offset) {
                    if (remaining[start + offset] < 3) {
                        body = false;
                        break;
                    }
                    remaining[start + offset] -= 3;
                }
                if (!body) continue;
                int singles = 0;
                for (int rank = 0; rank < RANK_COUNT; ++rank) {
                    if (rank < start || rank >= start + pattern.mainLength) {
                        singles += remaining[rank];
                    }
                }
                if (singles >= pattern.mainLength) return true;
            }
            return false;
        case CardPatternType::AirplaneWithPairs:
            for (int start = main + 1;
                 start <= rankWeight(Rank::Ace) - pattern.mainLength + 1; ++start) {
                RankCounts remaining = counts;
                bool body = true;
                for (int offset = 0; offset < pattern.mainLength; ++offset) {
                    if (remaining[start + offset] < 3) {
                        body = false;
                        break;
                    }
                    remaining[start + offset] -= 3;
                }
                if (!body) continue;
                int pairs = 0;
                for (const int count : remaining) {
                    if (count >= 2) ++pairs;
                }
                if (pairs >= pattern.mainLength) return true;
            }
            return false;
        case CardPatternType::FourWithTwoSingles:
            for (int four = main + 1; four < RANK_COUNT; ++four) {
                if (counts[four] < 4) continue;
                int singles = 0;
                for (int rank = 0; rank < RANK_COUNT; ++rank) {
                    if (rank != four) singles += counts[rank];
                }
                if (singles >= 2) return true;
            }
            return false;
        case CardPatternType::FourWithTwoPairs:
            for (int four = main + 1; four < RANK_COUNT; ++four) {
                if (counts[four] < 4) continue;
                int pairs = 0;
                for (int rank = 0; rank < RANK_COUNT; ++rank) {
                    if (rank != four && counts[rank] >= 2) ++pairs;
                }
                if (pairs >= 2) return true;
            }
            return false;
        default:
            return false;
    }
}

bool canBeat(const RankCounts& counts,
             const CardPattern& pattern) {
    int bestBombRank = -1;
    const int bestBombLevel = highestBombLevel(counts, &bestBombRank);
    if (!pattern.isBomb()) {
        return bestBombLevel > 0 || hasSamePatternResponse(counts, pattern);
    }
    const int currentLevel = bombLevel(pattern.type);
    return bestBombLevel > currentLevel ||
        (bestBombLevel == currentLevel && bestBombRank > rankWeight(pattern.mainRank));
}

bool isHostile(Role ownRole, Role otherRole) {
    if (ownRole == Role::Landlord) return otherRole == Role::Farmer;
    if (ownRole == Role::Farmer) return otherRole == Role::Landlord;
    return true;
}

} // namespace

StrategicSearchEvaluator::StrategicSearchEvaluator(
    const AiObservation& observation, const AiLevelProfile& profile)
    : m_observation(observation),
      m_profile(profile),
      m_publicSamples(buildPublicSamples()) {}

int StrategicSearchEvaluator::structuralQuality(const Hand& hand) const {
    const int groups = static_cast<int>(hand.groupByRank().size());
    const int loose = looseCardCount(hand);
    const int sequences = sequenceCoverage(hand, 1) + sequenceCoverage(hand, 2) +
        sequenceCoverage(hand, 3);
    return -groups * 18 - loose * 42 + sequences * 14 + bombCount(hand) * 55;
}

int StrategicSearchEvaluator::fastTurnEstimate(const Hand& hand) const {
    RankCounts counts{};
    for (const auto& card : hand.cards()) ++counts[rankWeight(card.rank())];
    int turns = 0;

    // Fold consecutive rank groups into a single turn. Longer, denser bodies
    // are reserved first because they are harder to rebuild with attachments.
    for (const auto [copies, minimumLength] :
         {std::pair{3, 2}, std::pair{2, 3}, std::pair{1, 5}}) {
        while (true) {
            int bestStart = -1;
            int bestLength = 0;
            int start = rankWeight(Rank::Three);
            while (start <= rankWeight(Rank::Ace)) {
                while (start <= rankWeight(Rank::Ace) && counts[start] < copies) ++start;
                int end = start;
                while (end <= rankWeight(Rank::Ace) && counts[end] >= copies) ++end;
                if (end - start > bestLength) {
                    bestStart = start;
                    bestLength = end - start;
                }
                start = std::max(end, start + 1);
            }
            if (bestLength < minimumLength) break;
            for (int rank = bestStart; rank < bestStart + bestLength; ++rank) {
                counts[rank] -= copies;
            }
            ++turns;
        }
    }

    int triples = 0;
    int pairs = 0;
    for (const int count : counts) {
        if (count <= 0) continue;
        ++turns;
        if (count >= 3) ++triples;
        else if (count >= 2) ++pairs;
    }
    turns -= std::min(triples, pairs);
    return std::max(1, turns);
}

int StrategicSearchEvaluator::estimateTurns(const Hand& hand, int depth,
                                             int& nodesRemaining) {
    if (hand.empty()) return 0;
    const std::uint64_t key = handKey(hand, depth);
    if (const auto cached = m_planCache.find(key); cached != m_planCache.end()) {
        return cached->second;
    }

    int best = fastTurnEstimate(hand);
    if (depth <= 0 || nodesRemaining <= 0) {
        m_planCache.emplace(key, best);
        return best;
    }

    auto moves = LegalMoveGenerator::generateLegalMoves(
        hand, std::nullopt, m_observation.publicState.activePlayerCount);
    std::sort(moves.begin(), moves.end(), [](const LegalMove& left,
                                             const LegalMove& right) {
        return typePriority(left) > typePriority(right);
    });
    const int beamWidth = std::clamp(m_profile.searchDepth * 2, 2, 8);
    const int examined = std::min({static_cast<int>(moves.size()), beamWidth,
                                   nodesRemaining});
    for (int index = 0; index < examined; ++index) {
        Hand remaining = hand;
        remaining.removeCards(idsOf(moves[index]));
        --nodesRemaining;
        best = std::min(best, 1 + estimateTurns(remaining, depth - 1, nodesRemaining));
        if (best == 1) break;
    }
    m_planCache.emplace(key, best);
    return best;
}

int StrategicSearchEvaluator::handPlanAdjustment(const Hand& remainingHand) {
    if (remainingHand.empty()) return 0;
    const int turns = fastTurnEstimate(remainingHand);
    return -turns * 235 + structuralQuality(remainingHand);
}

int StrategicSearchEvaluator::refinedHandPlanBonus(const Hand& remainingHand) {
    if (remainingHand.empty()) return 0;
    const int quickTurns = fastTurnEstimate(remainingHand);
    int nodes = std::max(6, m_profile.searchDepth * 4);
    const int refinedTurns = estimateTurns(remainingHand, m_profile.searchDepth, nodes);
    return (quickTurns - refinedTurns) * 190;
}

std::vector<StrategicSearchEvaluator::Sample>
StrategicSearchEvaluator::buildPublicSamples() const {
    const int sampleCount = std::max(0, m_profile.publicInferenceSamples);
    if (sampleCount == 0 || m_observation.phase != GamePhase::Playing) return {};

    std::array<bool, TOTAL_CARDS> unavailable{};
    for (const auto& card : m_observation.ownHand.cards()) unavailable[card.id()] = true;
    for (const auto& action : m_observation.publicState.actionHistory) {
        if (action.type != PublicActionType::Play) continue;
        for (const auto& card : action.cards) unavailable[card.id()] = true;
    }

    int landlord = -1;
    const int activePlayerCount = m_observation.publicState.activePlayerCount;
    const int totalCards = totalCardsForPlayerCount(activePlayerCount);
    for (int player = 0; player < activePlayerCount; ++player) {
        if (m_observation.publicState.players[player].role == Role::Landlord) {
            landlord = player;
            break;
        }
    }

    Sample fixed{};
    std::array<int, PLAYER_COUNT> capacities{};
    for (int player = 0; player < activePlayerCount; ++player) {
        if (player == static_cast<int>(m_observation.playerId)) continue;
        capacities[player] = std::max(0,
            m_observation.publicState.players[player].remainingCards);
    }

    if (m_observation.publicState.bottomCardsRevealed && landlord >= 0 &&
        landlord != static_cast<int>(m_observation.playerId)) {
        for (const auto& card : m_observation.publicState.bottomCards) {
            if (unavailable[card.id()] || capacities[landlord] <= 0) continue;
            ++fixed[landlord][rankWeight(card.rank())];
            --capacities[landlord];
            unavailable[card.id()] = true;
        }
    }

    std::vector<Card> unknownCards;
    for (int id = 0; id < totalCards; ++id) {
        if (!unavailable[id]) unknownCards.push_back(Card::create(static_cast<CardId>(id)));
    }
    const int required = std::accumulate(capacities.begin(), capacities.end(), 0);
    if (required <= 0 || static_cast<int>(unknownCards.size()) < required) return {};

    std::vector<Sample> samples;
    samples.reserve(sampleCount);
    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
        Sample sample = fixed;
        auto remainingCapacities = capacities;
        auto cards = unknownCards;
        std::mt19937_64 random(m_observation.decisionSeed ^ kSamplingSalt ^
                              (static_cast<std::uint64_t>(sampleIndex + 1) *
                               0x9E3779B97F4A7C15ULL));
        std::shuffle(cards.begin(), cards.end(), random);

        int assigned = 0;
        for (const auto& card : cards) {
            if (assigned >= required) break;
            std::array<int, PLAYER_COUNT> weights{};
            int totalWeight = 0;
            const bool highCard = card.rank() >= Rank::Ace;
            for (int player = 0; player < activePlayerCount; ++player) {
                if (remainingCapacities[player] <= 0) continue;
                const auto& publicPlayer = m_observation.publicState.players[player];
                int weight = remainingCapacities[player] * 100;
                if (highCard) weight += remainingCapacities[player] *
                    std::max(0, publicPlayer.bidScore) * 18;
                if (highCard && publicPlayer.hasPassedBid) weight = weight * 9 / 10;
                weights[player] = std::max(1, weight);
                totalWeight += weights[player];
            }
            if (totalWeight <= 0) break;
            std::uniform_int_distribution<int> choose(1, totalWeight);
            int ticket = choose(random);
            int selectedPlayer = 0;
            for (; selectedPlayer < activePlayerCount; ++selectedPlayer) {
                ticket -= weights[selectedPlayer];
                if (ticket <= 0) break;
            }
            selectedPlayer = std::min(selectedPlayer, activePlayerCount - 1);
            ++sample[selectedPlayer][rankWeight(card.rank())];
            --remainingCapacities[selectedPlayer];
            ++assigned;
        }
        if (assigned == required) samples.push_back(std::move(sample));
    }
    return samples;
}

int StrategicSearchEvaluator::publicInformationAdjustment(
    const LegalMove& move, const Hand& remainingHand, bool isLeader) const {
    if (m_publicSamples.empty() || remainingHand.empty()) return 0;

    const int ownIndex = static_cast<int>(m_observation.playerId);
    const Role ownRole = m_observation.publicState.players[ownIndex].role;
    double hostileRisk = 0.0;
    double teammateOpportunity = 0.0;
    for (const auto& sample : m_publicSamples) {
        double sampleRisk = 0.0;
        double sampleTeam = 0.0;
        const int activePlayerCount = m_observation.publicState.activePlayerCount;
        for (int player = 0; player < activePlayerCount; ++player) {
            if (player == ownIndex || !canBeat(sample[player], move.pattern)) continue;
            const auto& publicPlayer = m_observation.publicState.players[player];
            const int clockwiseDistance =
                (player - ownIndex + activePlayerCount) % activePlayerCount;
            const double turnWeight = clockwiseDistance == 1 ? 1.20 : 1.0;
            if (isHostile(ownRole, publicPlayer.role)) {
                const double endgameWeight = publicPlayer.remainingCards <= 2 ? 1.75 : 1.0;
                const double passEvidence = publicPlayer.lastActionWasPass ? 0.72 : 1.0;
                sampleRisk = std::max(sampleRisk,
                    turnWeight * endgameWeight * passEvidence);
            } else if (ownRole == Role::Farmer && publicPlayer.role == Role::Farmer) {
                const double finishWeight = publicPlayer.remainingCards <= 3 ? 1.35 : 0.55;
                sampleTeam = std::max(sampleTeam, turnWeight * finishWeight);
            }
        }
        hostileRisk += sampleRisk;
        teammateOpportunity += sampleTeam;
    }

    const double sampleCount = static_cast<double>(m_publicSamples.size());
    const double averageRisk = hostileRisk / sampleCount;
    const double averageTeam = teammateOpportunity / sampleCount;
    const int endgameScale = remainingHand.size() <= 5 ? 2 : 1;
    int adjustment = -static_cast<int>(std::lround(averageRisk *
        (isLeader ? 310.0 : 220.0) * endgameScale));
    if (isLeader) {
        adjustment += static_cast<int>(std::lround((1.0 - std::min(1.0, averageRisk)) *
                                                    115.0 * endgameScale));
        adjustment += static_cast<int>(std::lround(averageTeam * 45.0));
    }
    return adjustment;
}

} // namespace fpdz
