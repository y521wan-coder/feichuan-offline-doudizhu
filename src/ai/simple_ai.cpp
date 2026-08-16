#include "simple_ai.h"
#include "bidding_strategy.h"
#include "legal_move_generator.h"
#include "../core/rules/pattern_analyzer.h"
#include <algorithm>
namespace fpdz {
SimpleAiPlayer::SimpleAiPlayer(AiDifficulty difficulty)
    : m_difficulty(difficulty) {}

GameCommand SimpleAiPlayer::decideBid(const AiObservation& observation) {
    GameCommand cmd;
    cmd.type = GameCommandType::Bid;
    cmd.playerId = observation.playerId;
    cmd.bidValue = BiddingStrategy::decideBid(observation.ownHand,
                                              observation.publicState.bottomCards,
                                              observation.highestBid,
                                              m_difficulty, observation.decisionSeed,
                                              HeuristicWeights::defaults());
    return cmd;
}
GameCommand SimpleAiPlayer::decidePlay(const AiObservation& observation) {
    GameCommand cmd;
    cmd.playerId = observation.playerId;
    const bool isLeader = observation.publicState.lastPlayedCards.empty();
    if (isLeader) {
        auto moves = LegalMoveGenerator::generateFreePlayMoves(observation.ownHand);
        if (!moves.empty()) {
            cmd.type = GameCommandType::PlayCards;
            const size_t limit = std::min<size_t>(moves.size(), 4);
            const size_t choice = static_cast<size_t>(observation.decisionSeed % limit);
            for (const auto& card : moves[choice]) cmd.cardIds.push_back(card.id());
            return cmd;
        }
    } else {
        const CardPattern lastPattern = PatternAnalyzer::analyze(
            observation.publicState.lastPlayedCards);
        auto moves = LegalMoveGenerator::generateResponseMoves(observation.ownHand, lastPattern);
        if (!moves.empty()) {
            cmd.type = GameCommandType::PlayCards;
            for (const auto& card : moves.back()) cmd.cardIds.push_back(card.id());
            return cmd;
        }
    }
    cmd.type = GameCommandType::Pass;
    return cmd;
}
} // namespace fpdz
