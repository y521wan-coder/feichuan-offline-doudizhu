#include "standard_ai.h"

#include "bidding_strategy.h"
#include "farmer_team_strategy.h"
#include "landlord_strategy.h"
#include "../core/rules/pattern_analyzer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>

namespace fpdz {
namespace {

struct EvaluationPolicy {
    int freeMoveCardReward = 140;
    int responseMoveCardReward = 95;
    int leaderRankPenalty = 2;
    int responseRankPenalty = 8;
    int remainingCardPenalty = 8;
    int remainingGroupPenalty = 45;
    int finishBonus = 100000;
    int nearFinishBonus = 450;
    int followUpPenalty = 2;
    int largestFollowUpBonus = 35;
    int bombReserveBonus = 90;
    int normalBombPenalty = 700;
};

constexpr EvaluationPolicy kPolicy;

int bombReserveCount(const Hand& hand) {
    int count = 0;
    for (const auto& [rank, cards] : hand.groupByRank()) {
        (void)rank;
        if (cards.size() >= 4) ++count;
    }
    return count;
}

int remainingShapePenalty(const Hand& hand) {
    return hand.size() * kPolicy.remainingCardPenalty +
           static_cast<int>(hand.groupByRank().size()) * kPolicy.remainingGroupPenalty;
}

int publicRankPlayedCount(const PublicGameSnapshot& state, Rank rank) {
    int count = 0;
    for (const auto& action : state.actionHistory) {
        if (action.type != PublicActionType::Play) continue;
        for (const auto& card : action.cards) {
            if (card.rank() == rank) ++count;
        }
    }
    return count;
}

} // namespace

StandardAiPlayer::StandardAiPlayer(AiDifficulty difficulty)
    : m_profile(aiLevelProfile(difficulty)) {}

GameCommand StandardAiPlayer::decideBid(const AiObservation& observation) {
    GameCommand command;
    command.type = GameCommandType::Bid;
    command.playerId = observation.playerId;
    command.bidValue = BiddingStrategy::decideBid(
        observation.ownHand, observation.highestBid, m_profile.level,
        observation.decisionSeed);
    command.aiDecisionReason = "bid_from_own_25_cards";
    return command;
}

GameCommand StandardAiPlayer::decidePlay(const AiObservation& observation) {
    GameCommand command;
    command.playerId = observation.playerId;
    const bool isLeader = observation.publicState.lastPlayedCards.empty();
    std::optional<CardPattern> lastPattern;
    if (!isLeader) {
        lastPattern = PatternAnalyzer::analyze(observation.publicState.lastPlayedCards);
    }
    const auto moves = LegalMoveGenerator::generateLegalMoves(observation.ownHand, lastPattern);
    if (moves.empty()) {
        command.type = GameCommandType::Pass;
        command.aiDecisionReason = "no_legal_move";
        return command;
    }

    const Role ownRole = observation.publicState.players[
        static_cast<int>(observation.playerId)].role;
    const Role lastRole = isLeader ? Role::Undetermined
        : observation.publicState.players[
              static_cast<int>(observation.publicState.lastPlayedBy)].role;

    std::vector<std::size_t> candidateIndexes(moves.size());
    std::iota(candidateIndexes.begin(), candidateIndexes.end(), 0);
    if (ownRole == Role::Farmer) {
        const auto teamDecision = FarmerTeamStrategy::selectCandidates(
            observation, moves, isLeader);
        command.aiDecisionReason = teamDecision.reasonCode;
        command.aiTeamRuleException = teamDecision.teamRuleException;
        if (teamDecision.pass) {
            command.type = GameCommandType::Pass;
            return command;
        }
        candidateIndexes = teamDecision.allowedMoveIndexes;
    } else {
        command.aiDecisionReason = "landlord_hand_shape_search";
    }

    if (candidateIndexes.empty()) {
        command.type = GameCommandType::Pass;
        command.aiDecisionReason = "team_filter_no_safe_response";
        return command;
    }

    struct ScoredMove {
        std::size_t index = 0;
        int score = std::numeric_limits<int>::min();
    };
    std::vector<ScoredMove> scoredMoves;
    scoredMoves.reserve(candidateIndexes.size());
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(m_profile.decisionBudgetMs);

    // Candidate limits constrain look-ahead work, never legality or the team
    // safety filter. Every safe first move receives the deterministic base score.
    const int lookAheadLimit = std::max(1, m_profile.candidateLimit);
    int bestScore = std::numeric_limits<int>::min();
    for (const auto index : candidateIndexes) {
        const auto& move = moves[index];
        Hand remaining = observation.ownHand;
        std::vector<CardId> cardIds;
        cardIds.reserve(move.cards.size());
        for (const auto& card : move.cards) cardIds.push_back(card.id());
        remaining.removeCards(cardIds);

        int score = static_cast<int>(move.cards.size()) *
            (isLeader ? kPolicy.freeMoveCardReward : kPolicy.responseMoveCardReward);
        score -= rankWeight(move.pattern.mainRank) *
            (isLeader ? kPolicy.leaderRankPenalty : kPolicy.responseRankPenalty);
        score -= remainingShapePenalty(remaining);
        if (remaining.empty()) score += kPolicy.finishBonus;
        if (remaining.size() <= 2) score += kPolicy.nearFinishBonus;
        if (move.pattern.isBomb() && !remaining.empty()) score -= kPolicy.normalBombPenalty;

        if (std::chrono::steady_clock::now() < deadline && !remaining.empty()) {
            const auto followUps = LegalMoveGenerator::generateLegalMoves(remaining);
            score -= static_cast<int>(followUps.size()) * kPolicy.followUpPenalty;
            int largest = 0;
            int examined = 0;
            for (const auto& followUp : followUps) {
                largest = std::max(largest, static_cast<int>(followUp.cards.size()));
                if (++examined >= lookAheadLimit ||
                    std::chrono::steady_clock::now() >= deadline) break;
            }
            score += largest * kPolicy.largestFollowUpBonus * m_profile.searchDepth;
            score += bombReserveCount(remaining) * kPolicy.bombReserveBonus;
        }

        score += publicRankPlayedCount(observation.publicState, move.pattern.mainRank) * 2;
        score += ownRole == Role::Farmer
            ? FarmerTeamStrategy::scoreAdjustment(observation, move, isLeader, lastRole)
            : LandlordStrategy::scoreAdjustment(observation, move, isLeader);
        scoredMoves.push_back({index, score});
        bestScore = std::max(bestScore, score);
    }

    const int tolerance = std::max(4, std::abs(bestScore) / 100);
    std::vector<const ScoredMove*> safeNearBest;
    for (const auto& scored : scoredMoves) {
        if (bestScore - scored.score <= tolerance) safeNearBest.push_back(&scored);
    }
    const ScoredMove* selected = *std::max_element(
        safeNearBest.begin(), safeNearBest.end(),
        [](const ScoredMove* left, const ScoredMove* right) {
            return left->score < right->score;
        });

    if (safeNearBest.size() > 1 && m_profile.safeChoiceRandomness > 0.0) {
        std::mt19937_64 random(observation.decisionSeed ^ 0x534146455F43484FULL);
        std::bernoulli_distribution useRandomSafeChoice(m_profile.safeChoiceRandomness);
        if (useRandomSafeChoice(random)) {
            std::uniform_int_distribution<std::size_t> choose(0, safeNearBest.size() - 1);
            selected = safeNearBest[choose(random)];
        }
    }

    command.type = GameCommandType::PlayCards;
    for (const auto& card : moves[selected->index].cards) command.cardIds.push_back(card.id());
    return command;
}

} // namespace fpdz
