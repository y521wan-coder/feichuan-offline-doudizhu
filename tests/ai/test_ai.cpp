#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>
#include <array>
#include <chrono>
#include <set>

#include "ai/ai_level_profile.h"
#include "ai/ai_decision_request.h"
#include "ai/bidding_strategy.h"
#include "ai/legal_move_generator.h"
#include "ai/local_ai_decision_adapter.h"
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

} // namespace

class TestAi : public QObject {
    Q_OBJECT
private slots:
    void testAiBattleUsesOneFastModeAndTenSecondDefault() {
        AiBattleSettings settings;
        QVERIFY(settings.landlordMustLeadFirstTurn);
        QVERIFY(AiBattleSettings::fromJson(QJsonObject{}).landlordMustLeadFirstTurn);
        settings.landlordMustLeadFirstTurn = false;
        QVERIFY(!AiBattleSettings::fromJson(settings.toJson()).landlordMustLeadFirstTurn);
        settings.landlordMustLeadFirstTurn = true;
        QCOMPARE(settings.autoPassSeconds, 30);
        QVERIFY(settings.autoPassEnabled);
        settings.autoPassSeconds = 1800;
        QCOMPARE(AiBattleSettings::fromJson(settings.toJson()).autoPassSeconds, 1800);
        settings.autoPassSeconds = 9999;
        settings.normalize();
        QCOMPARE(settings.autoPassSeconds, 1800);
        settings.autoPassSeconds = 1;
        settings.normalize();
        QCOMPARE(settings.autoPassSeconds, 3);
        settings.autoPassEnabled = false;
        QCOMPARE(AiBattleSettings::fromJson(settings.toJson()).autoPassEnabled, false);
        settings.autoPassEnabled = true;
        settings.autoPassSeconds = 30;
        settings.playerCount = TWO_PLAYER_COUNT;
        auto& cloud = settings.seats[1];
        cloud.kind = SeatControllerKind::CloudAi;
        cloud.credentialId = QStringLiteral("test-credential");
        cloud.model = QStringLiteral("deepseek-v4-flash");
        cloud.strength = CloudStrength::Deep;
        settings.normalize();
        settings.strategyPrompt = QString::fromUtf8(u8"先计算本队出完的手数");
        QCOMPARE(AiBattleSettings::fromJson(settings.toJson()).strategyPrompt,
                 settings.strategyPrompt);

        QString reason;
        QVERIFY(settings.validForStart(&reason));
        QCOMPARE(settings.seats[1].strength, CloudStrength::Fast);
        QCOMPARE(settings.seats[1].timeoutSeconds, 10);

        QJsonObject legacy = settings.toJson();
        legacy[QStringLiteral("schemaVersion")] = 1;
        QJsonArray seats = legacy.value("seats").toArray();
        QJsonObject oldCloud = seats.at(1).toObject();
        oldCloud[QStringLiteral("strength")] = static_cast<int>(CloudStrength::Deep);
        oldCloud[QStringLiteral("timeoutSeconds")] = 15;
        seats[1] = oldCloud;
        legacy[QStringLiteral("seats")] = seats;
        const AiBattleSettings migrated = AiBattleSettings::fromJson(legacy);
        QCOMPARE(migrated.seats[1].strength, CloudStrength::Fast);
        QCOMPARE(migrated.seats[1].timeoutSeconds, 10);
        QCOMPARE(migrated.toJson().value("schemaVersion").toInt(), 2);

        oldCloud[QStringLiteral("timeoutSeconds")] = 300;
        seats[1] = oldCloud;
        legacy[QStringLiteral("seats")] = seats;
        QCOMPARE(AiBattleSettings::fromJson(legacy).seats[1].timeoutSeconds, 180);
    }

    void testAllComputerSeatsShareOneCloudModel() {
        AiBattleSettings settings;
        settings.playerCount = PLAYER_COUNT;
        settings.seats[1].kind = SeatControllerKind::CloudAi;
        settings.seats[1].credentialId = QStringLiteral("credential-a");
        settings.seats[1].credentialName = QString::fromUtf8(u8"认证A");
        settings.seats[1].model = QStringLiteral("shared-model");
        settings.seats[1].strength = CloudStrength::Fast;
        settings.seats[1].timeoutSeconds = 20;
        // 旧配置里其余座位可能还是本地机器人或其他模型。
        settings.seats[2].kind = SeatControllerKind::LocalAi;
        settings.seats[3].kind = SeatControllerKind::CloudAi;
        settings.seats[3].credentialId = QStringLiteral("credential-b");
        settings.seats[3].model = QStringLiteral("other-model");
        settings.normalize();

        for (int index = 1; index < PLAYER_COUNT; ++index) {
            const auto& seat = settings.seats[static_cast<std::size_t>(index)];
            QCOMPARE(seat.kind, SeatControllerKind::CloudAi);
            QCOMPARE(seat.credentialId, QStringLiteral("credential-a"));
            QCOMPARE(seat.model, QStringLiteral("shared-model"));
            QCOMPARE(seat.strength, CloudStrength::Fast);
            QCOMPARE(seat.timeoutSeconds, 20);
        }
        QVERIFY(settings.hasCloudSeat());
        QVERIFY(settings.validForStart(nullptr));
        // 玩家一仍为真人，不参与云配置。
        QCOMPARE(settings.seats[0].kind, SeatControllerKind::LocalAi);
    }

    void testCloudActionCatalogIsDeterministicBoundedAndEngineLegal() {
        for (const int playerCount : {TWO_PLAYER_COUNT, THREE_PLAYER_COUNT, PLAYER_COUNT}) {
            GameEngine engine;
            GameCommand start;
            start.type = GameCommandType::StartGame;
            start.playerCount = playerCount;
            start.randomSeed = 987600 + playerCount;
            QVERIFY(engine.execute(start).success);
            StandardAiPlayer ai(AiDifficulty::Advanced);
            while (engine.state().phase() == GamePhase::Bidding) {
                const auto player = engine.fullState().currentPlayer;
                QVERIFY(engine.execute(ai.decideBid(engine.state(), player)).success);
            }
            QCOMPARE(engine.state().phase(), GamePhase::Playing);
            const auto player = engine.fullState().currentPlayer;
            SeatControllerConfig controller;
            controller.kind = SeatControllerKind::CloudAi;
            controller.credentialId = QStringLiteral("test");
            controller.model = QStringLiteral("fake-model");
            const auto first = AiActionCatalog::create(
                engine.state(), player, controller, QStringLiteral("same-request"));
            const auto second = AiActionCatalog::create(
                engine.state(), player, controller, QStringLiteral("same-request"));
            QVERIFY(!first.actions.isEmpty());
            QVERIFY(first.actions.size() <= AI_MAX_ACTIONS);
            QCOMPARE(QJsonDocument(first.toServiceJson()).toJson(QJsonDocument::Compact),
                     QJsonDocument(second.toServiceJson()).toJson(QJsonDocument::Compact));
            QVERIFY(QJsonDocument(first.toServiceJson()).toJson(QJsonDocument::Compact).size()
                    <= AI_MAX_MESSAGE_BYTES);
            QSet<QString> signatures;
            for (const auto& action : first.actions) {
                QVERIFY(!signatures.contains(action.semanticSignature));
                signatures.insert(action.semanticSignature);
                GameEngine verifier;
                verifier.state() = engine.state();
                QVERIFY2(verifier.execute(action.command).success,
                         "Every catalog action must pass the real engine");
            }
        }
    }

    void testCloudResponseMustMatchWholeTurnContext() {
        GameEngine engine;
        GameCommand start;
        start.type = GameCommandType::StartGame;
        start.playerCount = THREE_PLAYER_COUNT;
        start.randomSeed = 20260924;
        QVERIFY(engine.execute(start).success);
        const PlayerId player = engine.fullState().currentPlayer;
        SeatControllerConfig controller;
        controller.kind = SeatControllerKind::CloudAi;
        controller.credentialId = QStringLiteral("test");
        controller.model = QStringLiteral("fake-model");
        const AiDecisionRequest request = AiActionCatalog::create(
            engine.state(), player, controller, QStringLiteral("request-1"));
        AiDecisionResponse response;
        response.requestId = request.requestId;
        response.gameId = request.gameId;
        response.eventSequence = request.eventSequence;
        response.phase = request.phase;
        response.playerId = request.playerId;
        QVERIFY(response.matches(request, engine.state()));

        auto changed = response;
        changed.gameId++;
        QVERIFY(!changed.matches(request, engine.state()));
        changed = response;
        changed.eventSequence++;
        QVERIFY(!changed.matches(request, engine.state()));
        changed = response;
        changed.playerId = PlayerId::Player4;
        QVERIFY(!changed.matches(request, engine.state()));
        const auto malformed = AiDecisionResponse::fromServiceJson({
            {QStringLiteral("request_id"), request.requestId},
            {QStringLiteral("game_id"), static_cast<qint64>(request.gameId)},
            {QStringLiteral("event_sequence"), static_cast<qint64>(request.eventSequence)},
            {QStringLiteral("phase"), QStringLiteral("unknown")},
            {QStringLiteral("seat"), 999}});
        QVERIFY(!malformed.matches(request, engine.state()));

        const GameCommand legalBid = request.actions.back().command;
        QVERIFY(engine.execute(legalBid).success);
        QVERIFY(!response.matches(request, engine.state()));
    }

    void testLocalDecisionAdapterMatchesFrozenStandardAi() {
        const std::array<int, 3> playerCounts{
            TWO_PLAYER_COUNT, THREE_PLAYER_COUNT, PLAYER_COUNT};
        const std::array<AiDifficulty, 3> difficulties{
            AiDifficulty::Beginner, AiDifficulty::Intermediate, AiDifficulty::Advanced};
        LocalAiDecisionAdapter adapter;
        for (const int playerCount : playerCounts) {
            for (const auto difficulty : difficulties) {
                GameEngine engine;
                GameCommand start;
                start.type = GameCommandType::StartGame;
                start.playerCount = playerCount;
                start.randomSeed = 20260924 + playerCount * 10 + static_cast<int>(difficulty);
                QVERIFY(engine.execute(start).success);

                StandardAiPlayer direct(difficulty);
                const auto biddingPlayer = engine.fullState().currentPlayer;
                const auto bidObservation = makeAiObservation(engine.state(), biddingPlayer);
                const auto directBid = direct.decideBid(bidObservation);
                const auto adaptedBid = adapter.requestBid(bidObservation, difficulty);
                QCOMPARE(adaptedBid.type, directBid.type);
                QCOMPARE(adaptedBid.playerId, directBid.playerId);
                QCOMPARE(adaptedBid.bidValue, directBid.bidValue);
                QCOMPARE(adaptedBid.aiDecisionReason, directBid.aiDecisionReason);

                for (int guard = 0; guard < 20 && engine.state().phase() == GamePhase::Bidding;
                     ++guard) {
                    const auto player = engine.fullState().currentPlayer;
                    QVERIFY(engine.execute(direct.decideBid(engine.state(), player)).success);
                }
                QCOMPARE(engine.state().phase(), GamePhase::Playing);
                const auto playingPlayer = engine.fullState().currentPlayer;
                const auto playObservation = makeAiObservation(engine.state(), playingPlayer);
                const auto directPlay = direct.decidePlay(playObservation);
                const auto adaptedPlay = adapter.requestPlay(playObservation, difficulty);
                QCOMPARE(adaptedPlay.type, directPlay.type);
                QCOMPARE(adaptedPlay.playerId, directPlay.playerId);
                QCOMPARE(adaptedPlay.cardIds, directPlay.cardIds);
                QCOMPARE(adaptedPlay.aiDecisionReason, directPlay.aiDecisionReason);
                QCOMPARE(adaptedPlay.aiTeamRuleException, directPlay.aiTeamRuleException);
            }
        }
    }

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

    void testFourPlayerGeneratorExcludesOverlappingAirplaneWings() {
        Hand hand;
        hand.addCards(sameRankCards(Rank::Three, 5));
        hand.addCards(sameRankCards(Rank::Four, 3));
        hand.addCards(sameRankCards(Rank::Five, 2));
        const auto moves = LegalMoveGenerator::generateLegalMoves(
            hand, std::nullopt, PLAYER_COUNT);
        QVERIFY(!containsResponse(moves, CardPatternType::AirplaneWithPairs, Rank::Three));

        hand.addCards(sameRankCards(Rank::Six, 2));
        const auto validMoves = LegalMoveGenerator::generateLegalMoves(
            hand, std::nullopt, PLAYER_COUNT);
        QVERIFY(containsResponse(validMoves, CardPatternType::AirplaneWithPairs, Rank::Three));
        for (const auto& move : validMoves) {
            if (move.pattern.type != CardPatternType::AirplaneWithPairs) continue;
            QCOMPARE(PatternAnalyzer::analyze(move.cards, PLAYER_COUNT).type,
                     CardPatternType::AirplaneWithPairs);
        }
    }

    void testSingleDeckGeneratorsAddStandardAttachmentsOnlyForSingleDeckModes() {
        Hand hand;
        hand.addCards(sameRankCards(Rank::Three, 3));
        hand.addCards(sameRankCards(Rank::Four, 3));
        hand.addCards(sameRankCards(Rank::Eight, 4));
        hand.addCard(Card::create(Rank::Six, Suit::Spades, 0));
        hand.addCard(Card::create(Rank::Seven, Suit::Hearts, 0));
        hand.addCard(Card::create(Rank::Nine, Suit::Clubs, 0));
        hand.addCard(Card::create(Rank::Ten, Suit::Diamonds, 0));

        const auto threePlayerMoves = LegalMoveGenerator::generateLegalMoves(
            hand, std::nullopt, THREE_PLAYER_COUNT);
        const auto twoPlayerMoves = LegalMoveGenerator::generateLegalMoves(
            hand, std::nullopt, TWO_PLAYER_COUNT);
        const auto fourPlayerMoves = LegalMoveGenerator::generateLegalMoves(
            hand, std::nullopt, PLAYER_COUNT);
        auto hasType = [](const std::vector<LegalMove>& moves, CardPatternType type) {
            return std::any_of(moves.begin(), moves.end(),
                [type](const LegalMove& move) { return move.pattern.type == type; });
        };
        QVERIFY(hasType(threePlayerMoves, CardPatternType::TripleWithSingle));
        QVERIFY(hasType(threePlayerMoves, CardPatternType::AirplaneWithSingles));
        QVERIFY(hasType(threePlayerMoves, CardPatternType::FourWithTwoSingles));
        QVERIFY(hasType(twoPlayerMoves, CardPatternType::TripleWithSingle));
        QVERIFY(hasType(twoPlayerMoves, CardPatternType::AirplaneWithSingles));
        QVERIFY(hasType(twoPlayerMoves, CardPatternType::FourWithTwoSingles));
        QVERIFY(!hasType(fourPlayerMoves, CardPatternType::TripleWithSingle));
        QVERIFY(!hasType(fourPlayerMoves, CardPatternType::AirplaneWithSingles));
        QVERIFY(!hasType(fourPlayerMoves, CardPatternType::FourWithTwoSingles));
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
        QCOMPARE(beginner.fullInformationRootCandidates, 2);
        QCOMPARE(intermediate.fullInformationRootCandidates, 3);
        QCOMPARE(advanced.fullInformationRootCandidates, 4);
        QCOMPARE(beginner.fullInformationNodeBudget, 120);
        QCOMPARE(intermediate.fullInformationNodeBudget, 500);
        QCOMPARE(advanced.fullInformationNodeBudget, 7000);
        QCOMPARE(beginner.rolloutPlies, 4);
        QCOMPARE(intermediate.rolloutPlies, 8);
        QCOMPARE(advanced.rolloutPlies, 20);
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

    void testPublicSnapshotCannotInjectHiddenBottomCards() {
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

    void testFullInformationSnapshotsAndPublicSeparation() {
        for (const int playerCount : {TWO_PLAYER_COUNT, THREE_PLAYER_COUNT, PLAYER_COUNT}) {
            GameEngine engine;
            GameCommand start;
            start.type = GameCommandType::StartGame;
            start.randomSeed = 4200 + playerCount;
            start.playerCount = playerCount;
            QVERIFY(engine.execute(start).success);
            const auto observation = makeAiObservation(
                engine.state(), engine.fullState().currentPlayer);

            QVERIFY(observation.fullInformation.available);
            QVERIFY(observation.publicState.bottomCards.empty());
            QVERIFY(!observation.publicState.bottomCardsRevealed);
            QCOMPARE(static_cast<int>(observation.fullInformation.hiddenBottomCards.size()),
                     bottomCardsForPlayerCount(playerCount));
            QCOMPARE(static_cast<int>(observation.fullInformation.setAsideCards.size()),
                     playerCount == TWO_PLAYER_COUNT ? TWO_PLAYER_SET_ASIDE_CARDS : 0);

            std::set<int> cardIds;
            for (int index = 0; index < PLAYER_COUNT; ++index) {
                const int expected = index < playerCount
                    ? cardsPerPlayerForPlayerCount(playerCount) : 0;
                QCOMPARE(observation.fullInformation.allHands[index].size(), expected);
                for (const auto& card : observation.fullInformation.allHands[index].cards()) {
                    QVERIFY(card.isValid());
                    QVERIFY(card.id() < totalCardsForPlayerCount(playerCount));
                    QVERIFY(cardIds.insert(card.id()).second);
                }
            }
            for (const auto& card : observation.fullInformation.hiddenBottomCards) {
                QVERIFY(cardIds.insert(card.id()).second);
            }
            for (const auto& card : observation.fullInformation.setAsideCards) {
                QVERIFY(cardIds.insert(card.id()).second);
            }
            QCOMPARE(static_cast<int>(cardIds.size()),
                     totalCardsForPlayerCount(playerCount));
        }
    }

    void testFullInformationBottomChangesAdvancedBid() {
        AiObservation weak;
        weak.playerId = PlayerId::Player1;
        weak.phase = GamePhase::Bidding;
        weak.publicState.activePlayerCount = THREE_PLAYER_COUNT;
        weak.ownHand.addCard(Card::create(Rank::Three, Suit::Spades, 0));
        weak.fullInformation.available = true;
        weak.fullInformation.allHands[0] = weak.ownHand;
        weak.fullInformation.allHands[1].addCard(
            Card::create(Rank::Four, Suit::Spades, 0));
        weak.fullInformation.allHands[2].addCard(
            Card::create(Rank::Five, Suit::Spades, 0));
        weak.fullInformation.hiddenBottomCards = {
            Card::create(Rank::Four, Suit::Hearts, 0),
            Card::create(Rank::Five, Suit::Hearts, 0),
            Card::create(Rank::Six, Suit::Hearts, 0)};
        AiObservation strong = weak;
        strong.fullInformation.hiddenBottomCards = {
            Card::create(Rank::Two, Suit::Spades, 0),
            Card::create(Rank::SmallJoker, Suit::None, 0),
            Card::create(Rank::BigJoker, Suit::None, 0)};

        StandardAiPlayer ai(AiDifficulty::Advanced);
        const int weakBid = ai.decideBid(weak).bidValue;
        const int strongBid = ai.decideBid(strong).bidValue;
        QVERIFY2(strongBid > weakBid,
                 "Exact hidden bottom cards must be able to change advanced bidding.");
    }

    void testOpponentHandsCanChangeAdvancedPlayDecision() {
        StandardAiPlayer ai(AiDifficulty::Advanced);
        bool foundChangedDecision = false;
        for (int seed = 1; seed <= 80 && !foundChangedDecision; ++seed) {
            GameEngine engine;
            GameCommand start;
            start.type = GameCommandType::StartGame;
            start.randomSeed = 880000 + seed;
            start.playerCount = THREE_PLAYER_COUNT;
            QVERIFY(engine.execute(start).success);
            int bidActions = 20;
            while (engine.state().phase() == GamePhase::Bidding && bidActions-- > 0) {
                const auto bidder = engine.fullState().currentPlayer;
                QVERIFY(engine.execute(ai.decideBid(engine.state(), bidder)).success);
            }
            QCOMPARE(engine.state().phase(), GamePhase::Playing);
            const auto landlord = engine.fullState().currentPlayer;
            auto first = makeAiObservation(engine.state(), landlord);
            auto second = first;
            const int own = static_cast<int>(landlord);
            std::array<int, 2> opponents{};
            int opponentCount = 0;
            for (int index = 0; index < THREE_PLAYER_COUNT; ++index) {
                if (index != own) opponents[opponentCount++] = index;
            }
            std::swap(second.fullInformation.allHands[opponents[0]],
                      second.fullInformation.allHands[opponents[1]]);
            QVERIFY(first.ownHand.cards() == second.ownHand.cards());
            QCOMPARE(first.publicState.actionHistory.size(),
                     second.publicState.actionHistory.size());
            for (int index = 0; index < PLAYER_COUNT; ++index) {
                QCOMPARE(first.publicState.players[index].remainingCards,
                         second.publicState.players[index].remainingCards);
            }
            const auto firstCommand = ai.decidePlay(first);
            const auto secondCommand = ai.decidePlay(second);
            foundChangedDecision = firstCommand.type != secondCommand.type ||
                firstCommand.cardIds != secondCommand.cardIds;
        }
        QVERIFY2(foundChangedDecision,
                 "Changing only real opponent hands must be able to change the play decision.");
    }

    void testDecisionDeadlinesReturnLegalCommands() {
        GameEngine engine;
        GameCommand start;
        start.type = GameCommandType::StartGame;
        start.randomSeed = 99123;
        start.playerCount = PLAYER_COUNT;
        QVERIFY(engine.execute(start).success);
        StandardAiPlayer bidding(AiDifficulty::Advanced);
        while (engine.state().phase() == GamePhase::Bidding) {
            const auto player = engine.fullState().currentPlayer;
            QVERIFY(engine.execute(bidding.decideBid(engine.state(), player)).success);
        }
        for (const auto difficulty : {AiDifficulty::Beginner,
                                      AiDifficulty::Intermediate,
                                      AiDifficulty::Advanced}) {
            StandardAiPlayer ai(difficulty);
            const auto player = engine.fullState().currentPlayer;
            const auto started = std::chrono::steady_clock::now();
            const auto command = ai.decidePlay(engine.state(), player);
            const double elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
            QVERIFY(!command.cardIds.empty() || command.type == GameCommandType::Pass);
            QVERIFY2(elapsed <= ai.decisionBudgetMilliseconds() + 100.0,
                     "AI exceeded its hard deadline tolerance.");
            GameEngine copy = engine;
            QVERIFY2(copy.execute(command).success,
                     "Deadline fallback must remain a legal command.");
        }
    }
};

QTEST_MAIN(TestAi)
#include "test_ai.moc"
