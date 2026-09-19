#include <QtTest>
#include <QJsonArray>

#include "ai/ai_level_profile.h"
#include "ai/bidding_strategy.h"
#include "ai/legal_move_generator.h"
#include "ai/simple_ai.h"
#include "ai/standard_ai.h"
#include "ai/strategic_search_evaluator.h"
#include "core/engine/game_engine.h"
#include "core/rules/pattern_analyzer.h"
#include "core/rules/pattern_comparator.h"

using namespace fpdz;

namespace {

std::vector<Card> sameRankCards(Rank rank, int count) {
    std::vector<Card> cards;
    const Suit suits[] = {Suit::Spades, Suit::Hearts, Suit::Clubs, Suit::Diamonds};
    for (int deck = 0; deck < 2 && static_cast<int>(cards.size()) < count; ++deck) {
        for (const Suit suit : suits) {
            if (static_cast<int>(cards.size()) >= count) break;
            cards.push_back(Card::create(rank, suit, static_cast<DeckIndex>(deck)));
        }
    }
    return cards;
}

bool containsResponse(const std::vector<LegalMove>& moves, CardPatternType type, Rank rank) {
    return std::any_of(moves.begin(), moves.end(), [&](const LegalMove& move) {
        return move.pattern.type == type && move.pattern.mainRank == rank;
    });
}

QJsonObject swapBottomCardsWithNonBidderHand(QJsonObject saved, PlayerId bidder) {
    QJsonArray players = saved["players"].toArray();
    const int bidderIndex = static_cast<int>(bidder);
    const int donorIndex = (bidderIndex + 1) % PLAYER_COUNT;
    QJsonObject donor = players[donorIndex].toObject();
    QJsonArray donorHand = donor["hand"].toArray();
    QJsonArray bottomCards = saved["bottomCards"].toArray();

    Q_ASSERT(bottomCards.size() == BOTTOM_CARDS);
    Q_ASSERT(donorHand.size() >= BOTTOM_CARDS);
    for (int index = 0; index < BOTTOM_CARDS; ++index) {
        const QJsonValue originalBottomCard = bottomCards[index];
        bottomCards[index] = donorHand[index];
        donorHand[index] = originalBottomCard;
    }

    donor["hand"] = donorHand;
    players[donorIndex] = donor;
    saved["players"] = players;
    saved["bottomCards"] = bottomCards;
    return saved;
}

} // namespace

class TestAi : public QObject {
    Q_OBJECT
private slots:
    void testAiBidReturnsValidCommand() {
        GameEngine engine;
        GameCommand start;
        start.type = GameCommandType::StartGame;
        start.randomSeed = 42;
        QVERIFY(engine.execute(start).success);
        SimpleAiPlayer ai;
        const auto command = ai.decideBid(engine.state(), engine.fullState().currentPlayer);
        QCOMPARE(command.type, GameCommandType::Bid);
        QVERIFY(command.bidValue >= 0 && command.bidValue <= 3);
    }

    void testAdvancedAiNeverPlaysIllegal() {
        StandardAiPlayer ai(AiDifficulty::Advanced);
        constexpr int kAdvancedStabilityGames = 500;
        int completedGames = 0;
        for (int seed = 1; seed <= kAdvancedStabilityGames; ++seed) {
            GameEngine engine;
            GameCommand start;
            start.type = GameCommandType::StartGame;
            start.randomSeed = seed;
            QVERIFY(engine.execute(start).success);
            for (int actions = 0; actions < 1000; ++actions) {
                const auto phase = engine.state().phase();
                if (phase == GamePhase::Finished) break;
                const auto player = engine.fullState().currentPlayer;
                const auto command = phase == GamePhase::Bidding
                    ? ai.decideBid(engine.state(), player)
                    : ai.decidePlay(engine.state(), player);
                QVERIFY2(engine.execute(command).success, "AI produced an illegal command");
            }
            QCOMPARE(engine.state().phase(), GamePhase::Finished);
            QVERIFY(engine.fullState().roundResult.valid);
            ++completedGames;
        }
        QCOMPARE(completedGames, kAdvancedStabilityGames);
    }

    void testGeneratedMovesAreAlwaysLegal() {
        for (int seed = 1; seed <= 50; ++seed) {
            GameEngine engine;
            GameCommand start;
            start.type = GameCommandType::StartGame;
            start.randomSeed = seed;
            QVERIFY(engine.execute(start).success);
            const auto moves = LegalMoveGenerator::generateLegalMoves(
                engine.fullState().players[0].hand);
            QVERIFY(!moves.empty());
            for (const auto& move : moves) {
                const auto analyzed = PatternAnalyzer::analyze(move.cards);
                QVERIFY(analyzed.isValid());
                QCOMPARE(analyzed.type, move.pattern.type);
            }
        }
    }

    void testGeneratedResponsesIncludeWinningMoves() {
        Hand hand;
        hand.addCards(sameRankCards(Rank::Four, 3));
        hand.addCards(sameRankCards(Rank::Two, 2));
        const auto tripleThree = PatternAnalyzer::analyze(sameRankCards(Rank::Three, 3));
        const auto pairAce = PatternAnalyzer::analyze(sameRankCards(Rank::Ace, 2));
        const auto tripleResponses = LegalMoveGenerator::generateLegalMoves(hand, tripleThree);
        const auto pairResponses = LegalMoveGenerator::generateLegalMoves(hand, pairAce);
        QVERIFY(containsResponse(tripleResponses, CardPatternType::Triple, Rank::Four));
        QVERIFY(containsResponse(pairResponses, CardPatternType::Pair, Rank::Two));
    }

    void testGeneratedMovesIncludeKingBomb() {
        Hand hand;
        hand.addCard(Card::create(Rank::SmallJoker, Suit::None, 0));
        hand.addCard(Card::create(Rank::BigJoker, Suit::None, 0));
        hand.addCards(sameRankCards(Rank::Three, 5));
        const auto freeMoves = LegalMoveGenerator::generateLegalMoves(hand);
        QVERIFY(containsResponse(freeMoves, CardPatternType::KingBomb, Rank::BigJoker));
    }

    void testThreeDifficultyProfiles() {
        const auto& beginner = aiLevelProfile(AiDifficulty::Beginner);
        const auto& intermediate = aiLevelProfile(AiDifficulty::Intermediate);
        const auto& advanced = aiLevelProfile(AiDifficulty::Advanced);
        QCOMPARE(beginner.decisionBudgetMs, 150);
        QCOMPARE(intermediate.decisionBudgetMs, 450);
        QCOMPARE(advanced.decisionBudgetMs, 1000);
        QVERIFY(beginner.searchDepth < intermediate.searchDepth);
        QVERIFY(intermediate.searchDepth < advanced.searchDepth);
        QVERIFY(beginner.candidateLimit < advanced.candidateLimit);
        QCOMPARE(beginner.publicInferenceSamples, 0);
        QVERIFY(intermediate.publicInferenceSamples > beginner.publicInferenceSamples);
        QVERIFY(advanced.publicInferenceSamples > intermediate.publicInferenceSamples);
    }

    void testStrategicPlannerPrefersCompactHandDecomposition() {
        Hand straightHand;
        for (int rank = static_cast<int>(Rank::Three);
             rank <= static_cast<int>(Rank::Seven); ++rank) {
            straightHand.addCard(Card::create(static_cast<Rank>(rank), Suit::Spades, 0));
        }
        Hand fragmentedHand;
        for (const Rank rank : {Rank::Three, Rank::Five, Rank::Seven,
                                Rank::Nine, Rank::Jack}) {
            fragmentedHand.addCard(Card::create(rank, Suit::Hearts, 0));
        }

        AiObservation observation;
        observation.phase = GamePhase::Playing;
        StrategicSearchEvaluator evaluator(observation,
                                            aiLevelProfile(AiDifficulty::Advanced));
        QVERIFY(evaluator.handPlanAdjustment(straightHand) >
                evaluator.handPlanAdjustment(fragmentedHand));
    }

    void testPublicPassEvidenceChangesOnlyPublicRiskScore() {
        AiObservation activeLandlord;
        activeLandlord.playerId = PlayerId::Player2;
        activeLandlord.phase = GamePhase::Playing;
        activeLandlord.decisionSeed = 912345;
        activeLandlord.ownHand.addCard(Card::create(Rank::Three, Suit::Spades, 0));
        activeLandlord.ownHand.addCard(Card::create(Rank::Four, Suit::Spades, 0));
        for (int index = 0; index < PLAYER_COUNT; ++index) {
            auto& player = activeLandlord.publicState.players[index];
            player.id = static_cast<PlayerId>(index);
            player.remainingCards = index == static_cast<int>(PlayerId::Player2) ? 2 : 20;
            player.role = Role::Farmer;
            player.roleRevealed = true;
        }
        activeLandlord.publicState.players[
            static_cast<int>(PlayerId::Player4)].role = Role::Landlord;

        const auto move = LegalMoveGenerator::generateLegalMoves(
            activeLandlord.ownHand).back();
        Hand remaining = activeLandlord.ownHand;
        std::vector<CardId> ids;
        for (const auto& card : move.cards) ids.push_back(card.id());
        QVERIFY(remaining.removeCards(ids));

        StrategicSearchEvaluator activeEvaluator(
            activeLandlord, aiLevelProfile(AiDifficulty::Advanced));
        const int activeRisk = activeEvaluator.publicInformationAdjustment(
            move, remaining, true);

        AiObservation passedLandlord = activeLandlord;
        passedLandlord.publicState.players[
            static_cast<int>(PlayerId::Player4)].lastActionWasPass = true;
        StrategicSearchEvaluator passedEvaluator(
            passedLandlord, aiLevelProfile(AiDifficulty::Advanced));
        const int passedRisk = passedEvaluator.publicInformationAdjustment(
            move, remaining, true);
        QVERIFY(passedRisk > activeRisk);

        StrategicSearchEvaluator repeatedEvaluator(
            activeLandlord, aiLevelProfile(AiDifficulty::Advanced));
        QCOMPARE(repeatedEvaluator.publicInformationAdjustment(move, remaining, true),
                 activeRisk);
    }

    void testBiddingIgnoresInjectedHiddenBottomCards() {
        Hand hand;
        hand.addCards(sameRankCards(Rank::Two, 4));
        AiObservation first;
        first.playerId = PlayerId::Player2;
        first.ownHand = hand;
        first.phase = GamePhase::Bidding;
        first.decisionSeed = 77;
        AiObservation second = first;
        second.publicState.bottomCards = sameRankCards(Rank::Ace, 8);
        second.publicState.bottomCardsRevealed = true;
        StandardAiPlayer ai(AiDifficulty::Advanced);
        QCOMPARE(ai.decideBid(first).bidValue, ai.decideBid(second).bidValue);
    }

    void testRestoredBiddingAiIgnoresHiddenBottomCards() {
        GameEngine engine;
        GameCommand start;
        start.type = GameCommandType::StartGame;
        start.randomSeed = 42;
        QVERIFY(engine.execute(start).success);

        const PlayerId bidder = engine.fullState().currentPlayer;
        QJsonObject firstSaved = engine.state().toJson();
        firstSaved["bottomCardsRevealed"] = true;
        QJsonObject secondSaved = swapBottomCardsWithNonBidderHand(firstSaved, bidder);

        const GameState firstRestored = GameState::fromJson(firstSaved);
        const GameState secondRestored = GameState::fromJson(secondSaved);
        QVERIFY(firstRestored.fullState().bottomCards !=
                secondRestored.fullState().bottomCards);
        QCOMPARE(static_cast<int>(firstRestored.fullState().bottomCards.size()),
                 BOTTOM_CARDS);
        QCOMPARE(static_cast<int>(secondRestored.fullState().bottomCards.size()),
                 BOTTOM_CARDS);

        const AiObservation first = makeAiObservation(firstRestored, bidder);
        const AiObservation second = makeAiObservation(secondRestored, bidder);
        QCOMPARE(first.phase, GamePhase::Bidding);
        QCOMPARE(second.phase, GamePhase::Bidding);
        QVERIFY(first.ownHand.cards() == second.ownHand.cards());
        QCOMPARE(first.decisionSeed, second.decisionSeed);
        QCOMPARE(first.highestBid, second.highestBid);
        QCOMPARE(first.publicState.gameId, second.publicState.gameId);
        QCOMPARE(first.publicState.currentPlayer, second.publicState.currentPlayer);
        QVERIFY(!first.publicState.bottomCardsRevealed);
        QVERIFY(!second.publicState.bottomCardsRevealed);
        QVERIFY(first.publicState.bottomCards.empty());
        QVERIFY(second.publicState.bottomCards.empty());
        for (int index = 0; index < PLAYER_COUNT; ++index) {
            QCOMPARE(first.publicState.players[index].remainingCards,
                     second.publicState.players[index].remainingCards);
        }

        StandardAiPlayer ai(AiDifficulty::Advanced);
        const GameCommand firstBid = ai.decideBid(first);
        const GameCommand secondBid = ai.decideBid(second);
        QCOMPARE(firstBid.type, secondBid.type);
        QCOMPARE(firstBid.playerId, secondBid.playerId);
        QCOMPARE(firstBid.bidValue, secondBid.bidValue);
        QCOMPARE(firstBid.aiDecisionReason, secondBid.aiDecisionReason);
    }
};

QTEST_MAIN(TestAi)
#include "test_ai.moc"
