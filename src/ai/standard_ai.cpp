#include "standard_ai.h"
#include "bidding_strategy.h"
#include "legal_move_generator.h"
#include "../core/rules/pattern_analyzer.h"
#include <algorithm>
#include <chrono>
#include <limits>
#include <random>

namespace fpdz {

namespace {

bool moveFinishesHand(const LegalMove& move, const Hand& hand) {
    return static_cast<int>(move.cards.size()) == hand.size();
}

int bombReserveCount(const Hand& hand) {
    int count = 0;
    for (const auto& [rank, cards] : hand.groupByRank()) {
        (void)rank;
        if (cards.size() >= 4) ++count;
    }
    return count;
}

int remainingShapePenalty(const Hand& hand, const HeuristicWeights& weights) {
    const int groups = static_cast<int>(hand.groupByRank().size());
    return hand.size() * weights.remainingCardPenalty +
           groups * weights.remainingGroupPenalty;
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

}

StandardAiPlayer::StandardAiPlayer(AiDifficulty difficulty)
    : m_difficulty(difficulty), m_weights(HeuristicWeights::defaults()) {}

StandardAiPlayer::StandardAiPlayer(AiDifficulty difficulty, const HeuristicWeights& weights)
    : m_difficulty(difficulty),
      m_weights(weights.isValid() ? weights : HeuristicWeights::defaults()) {}

int StandardAiPlayer::decisionBudgetMilliseconds() const {
    switch (m_difficulty) {
    case AiDifficulty::Beginner: return 150;
    case AiDifficulty::Intermediate: return 450;
    case AiDifficulty::Master: return 1000;
    }
    return 450;
}

GameCommand StandardAiPlayer::decideBid(const AiObservation& observation) {
    GameCommand command;
    command.type = GameCommandType::Bid;
    command.playerId = observation.playerId;
    command.bidValue = BiddingStrategy::decideBid(
        observation.ownHand, observation.publicState.bottomCards,
        observation.highestBid, m_difficulty, observation.decisionSeed, m_weights);
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
    auto moves = LegalMoveGenerator::generateLegalMoves(observation.ownHand, lastPattern);
    if (moves.empty()) {
        command.type = GameCommandType::Pass;
        return command;
    }

    const auto ownRole = observation.publicState.players[
        static_cast<int>(observation.playerId)].role;
    const auto lastRole = observation.publicState.players[
        static_cast<int>(observation.publicState.lastPlayedBy)].role;
    int landlordIndex = -1;
    for (int index = 0; index < PLAYER_COUNT; ++index) {
        if (observation.publicState.players[index].role == Role::Landlord) {
            landlordIndex = index;
            break;
        }
    }
    const bool landlordDanger = landlordIndex >= 0 &&
        observation.publicState.players[landlordIndex].remainingCards <= 2;
    const bool hasImmediateWin = std::any_of(moves.begin(), moves.end(),
        [&](const LegalMove& move) { return moveFinishesHand(move, observation.ownHand); });
    if (!isLeader && ownRole == Role::Farmer && lastRole == Role::Farmer &&
        !landlordDanger && !hasImmediateWin) {
        command.type = GameCommandType::Pass;
        return command;
    }

    struct ScoredMove {
        const LegalMove* move = nullptr;
        int score = std::numeric_limits<int>::min();
    };
    std::vector<ScoredMove> scoredMoves;
    scoredMoves.reserve(moves.size());
    int bestScore = std::numeric_limits<int>::min();
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(decisionBudgetMilliseconds());
    // All levels use this same bounded look-ahead. Their advertised time budget
    // also sets the node allowance, so a faster computer cannot accidentally
    // collapse all three levels into the same policy.
    const int lookAheadNodeLimit = std::max(1, decisionBudgetMilliseconds() / 10);
    for (const auto& move : moves) {
        Hand remaining = observation.ownHand;
        std::vector<CardId> cardIds;
        for (const auto& card : move.cards) cardIds.push_back(card.id());
        remaining.removeCards(cardIds);

        const bool finishesHand = remaining.empty();
        int score = static_cast<int>(move.cards.size()) *
            (isLeader ? m_weights.freeMoveCardReward : m_weights.responseMoveCardReward);
        score -= rankWeight(move.pattern.mainRank) *
            (isLeader ? m_weights.leaderRankPenalty : m_weights.responseRankPenalty);
        score -= remainingShapePenalty(remaining, m_weights);

        if (finishesHand) score += m_weights.finishBonus;
        if (!isLeader && lastRole == Role::Landlord) {
            score += landlordDanger ? m_weights.landlordDangerBonus
                                    : m_weights.landlordResponseBonus;
        }
        if (!isLeader && ownRole == Role::Farmer && lastRole == Role::Farmer) {
            score -= m_weights.farmerOvertakePenalty;
        }

        if (move.pattern.isBomb() && !finishesHand) {
            const bool urgentBomb = landlordDanger || (!isLeader && lastRole == Role::Landlord);
            score -= urgentBomb ? m_weights.urgentBombPenalty : m_weights.normalBombPenalty;
        }
        if (remaining.size() <= 2) score += m_weights.nearFinishBonus;

        // Every tier uses the same engine. Lower tiers differ by their model and
        // time budget, not by receiving a fundamentally different policy.
        int largestFollowUp = 0;
        if (std::chrono::steady_clock::now() < deadline) {
            const auto followUps = LegalMoveGenerator::generateLegalMoves(remaining);
            score -= static_cast<int>(followUps.size()) * m_weights.followUpPenalty;
            int bestBoundedFollowUp = 0;
            int examined = 0;
            for (const auto& followUp : followUps) {
                largestFollowUp = std::max(largestFollowUp,
                    static_cast<int>(followUp.cards.size()));
                if (examined++ >= lookAheadNodeLimit ||
                    std::chrono::steady_clock::now() >= deadline) break;
                bestBoundedFollowUp = std::max(bestBoundedFollowUp,
                    static_cast<int>(followUp.cards.size()) * m_weights.freeMoveCardReward -
                    rankWeight(followUp.pattern.mainRank) * m_weights.leaderRankPenalty);
            }
            score += largestFollowUp * m_weights.largestFollowUpBonus;
            score += bestBoundedFollowUp / 4;
            score += bombReserveCount(remaining) *
                (landlordDanger ? m_weights.bombReserveDangerBonus
                                : m_weights.bombReserveBonus);
            if (landlordDanger) {
                score += static_cast<int>(move.cards.size()) * m_weights.dangerMoveCardBonus;
            }
        }
        if (remaining.size() <= 12 && std::chrono::steady_clock::now() < deadline) {
            score += largestFollowUp * m_weights.endgameLargestBonus;
            score -= remainingShapePenalty(remaining, m_weights) *
                     m_weights.endgameShapeMultiplier;
        }
        // Public history is safe to use and helps estimate whether committing a
        // high rank is costly. No hidden hand is consulted here.
        score += publicRankPlayedCount(observation.publicState, move.pattern.mainRank) * 2;
        scoredMoves.push_back({&move, score});
        bestScore = std::max(bestScore, score);
    }

    const int tolerance = std::max(4, std::abs(bestScore) / 100);
    std::vector<const ScoredMove*> nearBest;
    std::vector<double> weights;
    for (const auto& scored : scoredMoves) {
        const int gap = bestScore - scored.score;
        if (gap > tolerance) continue;
        nearBest.push_back(&scored);
        weights.push_back(static_cast<double>(tolerance - gap + 1));
    }
    const ScoredMove* selected = nearBest.front();
    if (nearBest.size() > 1) {
        std::mt19937_64 policyRandom(observation.decisionSeed ^ 0x504F4C4943595F32ULL);
        std::discrete_distribution<size_t> choice(weights.begin(), weights.end());
        selected = nearBest[choice(policyRandom)];
    }

    command.type = GameCommandType::PlayCards;
    for (const auto& card : selected->move->cards) command.cardIds.push_back(card.id());
    return command;
}
} // namespace fpdz
