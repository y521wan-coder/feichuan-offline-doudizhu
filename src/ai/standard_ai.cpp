#include "standard_ai.h"

#include "bidding_strategy.h"
#include "farmer_team_strategy.h"
#include "imperfect_information_search.h"
#include "landlord_strategy.h"
#include "strategic_search_evaluator.h"
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
        observation.decisionSeed, observation.publicState.activePlayerCount);
    command.aiDecisionReason = "bid_from_own_hand";
    return command;
}

GameCommand StandardAiPlayer::decidePlay(const AiObservation& observation) {
    GameCommand command;
    command.playerId = observation.playerId;
    const bool isLeader = observation.publicState.lastPlayedCards.empty();
    std::optional<CardPattern> lastPattern;
    if (!isLeader) {
        lastPattern = PatternAnalyzer::analyze(
            observation.publicState.lastPlayedCards,
            observation.publicState.activePlayerCount);
    }
    const auto moves = LegalMoveGenerator::generateLegalMoves(
        observation.ownHand, lastPattern, observation.publicState.activePlayerCount);
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
        std::optional<int> publicSearchScore;
    };
    std::vector<ScoredMove> scoredMoves;
    scoredMoves.reserve(candidateIndexes.size());
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(m_profile.decisionBudgetMs);
    StrategicSearchEvaluator strategicSearch(observation, m_profile);

    // Candidate limits constrain look-ahead work, never legality or the team
    // safety filter. Every safe first move receives the deterministic base score.
    const int lookAheadLimit = std::max(1, m_profile.candidateLimit);
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
            const auto followUps = LegalMoveGenerator::generateLegalMoves(
                remaining, std::nullopt, observation.publicState.activePlayerCount);
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

        // The bounded planner compares complete future hand decompositions, so
        // attachments, sequence preservation and control-card reserves are
        // evaluated together instead of one move at a time. Public inference
        // samples only cards consistent with AiObservation's public facts.
        score += strategicSearch.handPlanAdjustment(remaining);
        score += strategicSearch.publicInformationAdjustment(move, remaining, isLeader);

        score += publicRankPlayedCount(observation.publicState, move.pattern.mainRank) * 2;
        score += ownRole == Role::Farmer
            ? FarmerTeamStrategy::scoreAdjustment(observation, move, isLeader, lastRole)
            : LandlordStrategy::scoreAdjustment(observation, move, isLeader);
        scoredMoves.push_back({index, score});
    }

    // Beam-search only the strongest quick candidates. All legal/team-safe
    // moves received the same deterministic shape and public-risk evaluation;
    // the bounded deeper decomposition is reserved for this shortlist.
    std::sort(scoredMoves.begin(), scoredMoves.end(),
        [](const ScoredMove& left, const ScoredMove& right) {
            if (left.score != right.score) return left.score > right.score;
            return left.index < right.index;
        });
    const int planningLimit = std::min(static_cast<int>(scoredMoves.size()),
                                       std::max(2, m_profile.searchDepth * 2));
    for (int position = 0; position < planningLimit; ++position) {
        const auto& move = moves[scoredMoves[position].index];
        Hand remaining = observation.ownHand;
        std::vector<CardId> cardIds;
        cardIds.reserve(move.cards.size());
        for (const auto& card : move.cards) cardIds.push_back(card.id());
        remaining.removeCards(cardIds);
        scoredMoves[position].score += strategicSearch.refinedHandPlanBonus(remaining);
    }
    scoredMoves.resize(planningLimit);

    // In tactically relevant positions, compare the best shape candidates by
    // actually rotating the table through public-information deal samples.
    // This is deliberately downstream of the shared farmer safety filter.
    std::sort(scoredMoves.begin(), scoredMoves.end(),
        [](const ScoredMove& left, const ScoredMove& right) {
            if (left.score != right.score) return left.score > right.score;
            return left.index < right.index;
        });
    ImperfectInformationSearch publicSearch(observation, m_profile);
    if (publicSearch.active()) {
        const int rolloutLimit = std::min(
            static_cast<int>(scoredMoves.size()),
            m_profile.level == AiDifficulty::Advanced ? 4 : 2);
        for (int position = 0; position < rolloutLimit; ++position) {
            auto& scored = scoredMoves[position];
            scored.publicSearchScore = publicSearch.scoreMove(moves[scored.index]);
            if (scored.publicSearchScore.has_value()) {
                scored.score += *scored.publicSearchScore / 2;
            }
        }
        scoredMoves.resize(rolloutLimit);
    }

    const int bestScore = std::max_element(
        scoredMoves.begin(), scoredMoves.end(),
        [](const ScoredMove& left, const ScoredMove& right) {
            return left.score < right.score;
        })->score;

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

    // A farmer may deliberately let the next farmer answer the landlord when
    // public card counts and sampled continuations show a materially safer team
    // result. This is not used to overtake a teammate and does not weaken the
    // existing mandatory one-card-landlord interception rule.
    if (!isLeader && ownRole == Role::Farmer && lastRole == Role::Landlord &&
        publicSearch.active()) {
        const int ownIndex = static_cast<int>(observation.playerId);
        const int nextIndex = (ownIndex + 1) % observation.publicState.activePlayerCount;
        const auto& nextPlayer = observation.publicState.players[nextIndex];
        const int landlordCards = observation.publicState.players[
            static_cast<int>(observation.publicState.lastPlayedBy)].remainingCards;
        if (nextPlayer.role == Role::Farmer && nextPlayer.remainingCards <= 2 &&
            landlordCards > 1) {
            const auto passScore = publicSearch.scorePass();
            const bool forcedTeammateFinish = passScore.has_value() &&
                selected->publicSearchScore.has_value() && *passScore >= 11000 &&
                *passScore >= *selected->publicSearchScore;
            const bool materiallySaferPass = passScore.has_value() &&
                selected->publicSearchScore.has_value() &&
                *passScore > *selected->publicSearchScore +
                    (m_profile.level == AiDifficulty::Advanced ? 150 : 350);
            if (forcedTeammateFinish || materiallySaferPass) {
                command.type = GameCommandType::Pass;
                command.aiDecisionReason = "strategic_pass_for_teammate_finish";
                return command;
            }
        }
    }

    command.type = GameCommandType::PlayCards;
    for (const auto& card : moves[selected->index].cards) command.cardIds.push_back(card.id());
    return command;
}

} // namespace fpdz
