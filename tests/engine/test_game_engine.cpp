#include <QtTest>
#include "core/engine/game_engine.h"
#include <QJsonArray>
#include "ai/simple_ai.h"
#include "ai/standard_ai.h"
using namespace fpdz;

namespace {

std::vector<Card> sameRankCards(Rank rank, int count) {
    std::vector<Card> cards;
    const Suit suits[] = {Suit::Spades, Suit::Hearts, Suit::Clubs, Suit::Diamonds};
    for (int deck = 0; deck < 2 && static_cast<int>(cards.size()) < count; ++deck) {
        for (Suit suit : suits) {
            if (static_cast<int>(cards.size()) >= count) break;
            cards.push_back(Card::create(rank, suit, static_cast<DeckIndex>(deck)));
        }
    }
    return cards;
}

std::vector<CardId> idsOf(const std::vector<Card>& cards) {
    std::vector<CardId> ids;
    for (const auto& card : cards) ids.push_back(card.id());
    return ids;
}

void forcePlayingResponseState(GameEngine& engine,
                               const std::vector<Card>& humanCards,
                               const std::vector<Card>& lastPlayedCards) {
    engine.state().setPhase(GamePhase::Playing);
    auto& fs = engine.state().fullState();
    fs.currentPlayer = PlayerId::Player1;
    fs.lastPlayedBy = PlayerId::Player4;
    fs.lastPlayedCards = lastPlayedCards;
    fs.consecutivePasses = 0;
    fs.players[0].hand.clear();
    fs.players[0].hand.addCards(humanCards);
    fs.players[0].role = Role::Farmer;
    fs.players[3].role = Role::Landlord;
}

}

class TestGameEngine : public QObject {
    Q_OBJECT
private slots:
    void testStartGame() {
        GameEngine engine;
        GameCommand cmd;
        cmd.type = GameCommandType::StartGame;
        cmd.randomSeed = 42;
        auto result = engine.execute(cmd);
        QVERIFY(result.success);
        QCOMPARE(engine.state().phase(), GamePhase::Bidding);
        // Verify each player has 25 cards
        for (int i = 0; i < PLAYER_COUNT; ++i) {
            QCOMPARE(engine.state().fullState().players[i].hand.size(), CARDS_PER_PLAYER);
        }
        QCOMPARE(static_cast<int>(engine.state().fullState().bottomCards.size()), BOTTOM_CARDS);
        QVERIFY(!engine.state().fullState().bottomCardsRevealed);
        const auto revealEvent = std::find_if(result.events.begin(), result.events.end(),
            [](const GameEvent& event) {
                return event.type == GameEventType::BottomCardsRevealed;
            });
        QVERIFY(revealEvent == result.events.end());
        QVERIFY(engine.publicSnapshot().bottomCards.empty());

        GameCommand bid;
        bid.type = GameCommandType::Bid;
        bid.playerId = engine.fullState().currentPlayer;
        bid.bidValue = 3;
        const auto bidResult = engine.execute(bid);
        QVERIFY(bidResult.success);
        QVERIFY(engine.fullState().bottomCardsRevealed);
        QCOMPARE(static_cast<int>(engine.publicSnapshot().bottomCards.size()), BOTTOM_CARDS);
        const auto publicReveal = std::find_if(bidResult.events.begin(), bidResult.events.end(),
            [](const GameEvent& event) {
                return event.type == GameEventType::BottomCardsRevealed;
            });
        QVERIFY(publicReveal != bidResult.events.end());
        QCOMPARE(static_cast<int>(publicReveal->cards.size()), BOTTOM_CARDS);
    }

    void testPauseResume() {
        GameEngine engine;
        GameCommand startCmd;
        startCmd.type = GameCommandType::StartGame;
        startCmd.randomSeed = 42;
        engine.execute(startCmd);

        // Force to playing by setting phase directly for testing
        engine.state().setPhase(GamePhase::Playing);

        GameCommand pauseCmd;
        pauseCmd.type = GameCommandType::Pause;
        auto result = engine.execute(pauseCmd);
        QVERIFY(result.success);
        QCOMPARE(engine.state().phase(), GamePhase::Paused);

        GameCommand resumeCmd;
        resumeCmd.type = GameCommandType::Resume;
        result = engine.execute(resumeCmd);
        QVERIFY(result.success);
        QCOMPARE(engine.state().phase(), GamePhase::Playing);
    }

    void testCannotStartTwice() {
        GameEngine engine;
        GameCommand cmd;
        cmd.type = GameCommandType::StartGame;
        cmd.randomSeed = 42;
        engine.execute(cmd);

        // Try to start again while game is in progress
        auto result = engine.execute(cmd);
        QVERIFY(!result.success);
        QCOMPARE(result.errorCode, ErrorCode::GameAlreadyStarted);
    }

    void testPublicSnapshotNoHiddenCards() {
        GameEngine engine;
        GameCommand cmd;
        cmd.type = GameCommandType::StartGame;
        cmd.randomSeed = 42;
        engine.execute(cmd);

        auto snap = engine.publicSnapshot();
        QVERIFY(!snap.bottomCardsRevealed);
        QVERIFY(snap.bottomCards.empty());
        for (const auto& player : snap.players) {
            QCOMPARE(player.remainingCards, CARDS_PER_PLAYER);
        }
    }

    void testPublicActionHistoryPersistsWithoutHiddenHands() {
        GameEngine engine;
        GameCommand start;
        start.type = GameCommandType::StartGame;
        start.randomSeed = 42;
        QVERIFY(engine.execute(start).success);
        QVERIFY(engine.publicSnapshot().actionHistory.empty());

        const auto bidder = engine.state().fullState().currentPlayer;
        GameCommand bid;
        bid.type = GameCommandType::Bid;
        bid.playerId = bidder;
        bid.bidValue = 3;
        QVERIFY(engine.execute(bid).success);
        const auto publicAfterBid = engine.publicSnapshot();
        QCOMPARE(publicAfterBid.actionHistory.size(), size_t(1));
        QCOMPARE(publicAfterBid.actionHistory.front().type, PublicActionType::Bid);
        QCOMPARE(publicAfterBid.actionHistory.front().playerId, bidder);
        QCOMPARE(publicAfterBid.actionHistory.front().bidValue, 3);
        QVERIFY(publicAfterBid.actionHistory.front().cards.empty());

        StandardAiPlayer ai(AiDifficulty::Advanced);
        const auto play = ai.decidePlay(engine.state(), bidder);
        QVERIFY(engine.execute(play).success);
        const auto publicAfterPlay = engine.publicSnapshot();
        QCOMPARE(publicAfterPlay.actionHistory.size(), size_t(2));
        QCOMPARE(publicAfterPlay.actionHistory.back().type, PublicActionType::Play);
        QVERIFY(!publicAfterPlay.actionHistory.back().cards.empty());

        const auto restored = GameState::fromJson(engine.state().toJson());
        QCOMPARE(restored.publicSnapshot().actionHistory.size(),
                 publicAfterPlay.actionHistory.size());
        QCOMPARE(restored.publicSnapshot().actionHistory.back().cards.size(),
                 publicAfterPlay.actionHistory.back().cards.size());
    }

    void testLegacyBiddingSaveMigratesCompleteBottomCardsToPublic() {
        GameEngine engine;
        GameCommand start;
        start.type = GameCommandType::StartGame;
        start.randomSeed = 42;
        QVERIFY(engine.execute(start).success);

        QJsonObject saved = engine.state().toJson();
        saved["bottomCardsRevealed"] = false;
        const GameState restored = GameState::fromJson(saved);
        QCOMPARE(restored.phase(), GamePhase::Bidding);
        QVERIFY(restored.fullState().bottomCardsRevealed);
        QCOMPARE(static_cast<int>(restored.fullState().bottomCards.size()), BOTTOM_CARDS);
    }

    void testDamagedBiddingSaveDoesNotRevealIncompleteBottomCards() {
        GameEngine engine;
        GameCommand start;
        start.type = GameCommandType::StartGame;
        start.randomSeed = 42;
        QVERIFY(engine.execute(start).success);

        QJsonObject saved = engine.state().toJson();
        QJsonArray bottomCards = saved["bottomCards"].toArray();
        bottomCards.removeLast();
        saved["bottomCards"] = bottomCards;
        saved["bottomCardsRevealed"] = false;
        const GameState restored = GameState::fromJson(saved);
        QVERIFY(!restored.fullState().bottomCardsRevealed);
        QCOMPARE(static_cast<int>(restored.fullState().bottomCards.size()), BOTTOM_CARDS - 1);
    }

    void testLandlordDeterminationRevealsBottomCardsExactlyOnce() {
        GameEngine engine;
        GameCommand start;
        start.type = GameCommandType::StartGame;
        start.randomSeed = 42;
        QVERIFY(engine.execute(start).success);

        GameCommand bid;
        bid.type = GameCommandType::Bid;
        bid.playerId = engine.state().fullState().currentPlayer;
        bid.bidValue = 3;
        const auto result = engine.execute(bid);
        QVERIFY(result.success);
        QCOMPARE(engine.state().phase(), GamePhase::Playing);
        const int revealEvents = static_cast<int>(std::count_if(
            result.events.begin(), result.events.end(), [](const GameEvent& event) {
                return event.type == GameEventType::BottomCardsRevealed;
            }));
        QCOMPARE(revealEvents, 1);
    }

    void testBiddingStartPlayerDependsOnSeed() {
        GameEngine first;
        GameCommand cmd;
        cmd.type = GameCommandType::StartGame;
        cmd.randomSeed = 1;
        auto result = first.execute(cmd);
        QVERIFY(result.success);
        const auto firstSeat = first.state().fullState().currentPlayer;
        bool foundDifferentSeat = false;
        for (uint64_t seed = 2; seed <= 64 && !foundDifferentSeat; ++seed) {
            GameEngine other;
            cmd.randomSeed = seed;
            result = other.execute(cmd);
            QVERIFY(result.success);
            foundDifferentSeat = other.state().fullState().currentPlayer != firstSeat;
        }
        QVERIFY(foundDifferentSeat);
    }

    void testAllPlayersPassBidRedealsImmediately() {
        GameEngine engine;
        GameCommand startCmd;
        startCmd.type = GameCommandType::StartGame;
        startCmd.randomSeed = 4;
        auto result = engine.execute(startCmd);
        QVERIFY(result.success);

        const auto firstGameId = engine.state().gameId();
        for (int i = 0; i < PLAYER_COUNT; ++i) {
            GameCommand bidCmd;
            bidCmd.type = GameCommandType::Bid;
            bidCmd.playerId = engine.state().fullState().currentPlayer;
            bidCmd.bidValue = 0;
            result = engine.execute(bidCmd);
            QVERIFY(result.success);
        }

        QCOMPARE(engine.state().phase(), GamePhase::Bidding);
        QVERIFY(engine.state().gameId() != firstGameId);
        for (int i = 0; i < PLAYER_COUNT; ++i) {
            QCOMPARE(engine.state().fullState().players[i].hand.size(), CARDS_PER_PLAYER);
        }
    }

    void testDeterministicSeedRemainsDeterministicAfterRedeal() {
        auto forceAllPass = [](GameEngine& engine) {
            GameCommand start;
            start.type = GameCommandType::StartGame;
            start.randomSeed = 4;
            QVERIFY(engine.execute(start).success);
            for (int i = 0; i < PLAYER_COUNT; ++i) {
                GameCommand pass;
                pass.type = GameCommandType::Bid;
                pass.playerId = engine.state().fullState().currentPlayer;
                pass.bidValue = 0;
                QVERIFY(engine.execute(pass).success);
            }
        };
        GameEngine first;
        GameEngine second;
        forceAllPass(first);
        forceAllPass(second);
        QVERIFY(first.state().fullState().deterministicRandom);
        QCOMPARE(first.state().toJson(), second.state().toJson());
    }

    void testFourthPlayerCanBidAfterThreePasses() {
        GameEngine engine;
        GameCommand startCmd;
        startCmd.type = GameCommandType::StartGame;
        startCmd.randomSeed = 4;
        QVERIFY(engine.execute(startCmd).success);
        const auto firstGameId = engine.state().gameId();

        for (int count = 0; count < 3; ++count) {
            GameCommand passBid;
            passBid.type = GameCommandType::Bid;
            passBid.playerId = engine.state().fullState().currentPlayer;
            passBid.bidValue = 0;
            QVERIFY(engine.execute(passBid).success);
        }

        QCOMPARE(engine.state().gameId(), firstGameId);
        QCOMPARE(engine.state().phase(), GamePhase::Bidding);
        GameCommand finalBid;
        finalBid.type = GameCommandType::Bid;
        finalBid.playerId = engine.state().fullState().currentPlayer;
        finalBid.bidValue = 2;
        QVERIFY(engine.execute(finalBid).success);
        QCOMPARE(engine.state().phase(), GamePhase::Playing);
        QCOMPARE(engine.state().fullState().baseScore, 2);
    }

    void testLandlordHandIsSortedAfterReceivingBottomCards() {
        GameEngine engine;
        GameCommand start;
        start.type = GameCommandType::StartGame;
        start.randomSeed = 42;
        QVERIFY(engine.execute(start).success);

        GameCommand bid;
        bid.type = GameCommandType::Bid;
        bid.playerId = engine.state().fullState().currentPlayer;
        bid.bidValue = 3;
        QVERIFY(engine.execute(bid).success);
        QCOMPARE(engine.state().phase(), GamePhase::Playing);

        const auto landlordId = engine.state().fullState().highestBidder;
        const auto& cards = engine.state().fullState()
            .players[static_cast<int>(landlordId)].hand.cards();
        QCOMPARE(static_cast<int>(cards.size()), LANDLORD_TOTAL);
        for (size_t index = 1; index < cards.size(); ++index) {
            QVERIFY(cards[index - 1].weight() <= cards[index].weight());
        }
    }

    void testTimeoutPassCanSkipLeaderButManualPassCannot() {
        GameEngine engine;
        GameCommand startCmd;
        startCmd.type = GameCommandType::StartGame;
        startCmd.randomSeed = 42;
        auto result = engine.execute(startCmd);
        QVERIFY(result.success);

        engine.state().setPhase(GamePhase::Playing);
        engine.state().fullState().currentPlayer = PlayerId::Player1;
        engine.state().fullState().lastPlayedCards.clear();

        GameCommand manualPass;
        manualPass.type = GameCommandType::Pass;
        manualPass.playerId = PlayerId::Player1;
        result = engine.execute(manualPass);
        QVERIFY(!result.success);
        QCOMPARE(result.errorCode, ErrorCode::CannotPassAsLeader);

        GameCommand timeoutPass = manualPass;
        timeoutPass.allowPassAsLeader = true;
        result = engine.execute(timeoutPass);
        QVERIFY(result.success);
        QCOMPARE(engine.state().fullState().currentPlayer, PlayerId::Player2);
        QVERIFY(engine.state().fullState().lastPlayedCards.empty());
    }

    void testTripleFourCanBeatTripleThreeInPlayFlow() {
        GameEngine engine;
        GameCommand startCmd;
        startCmd.type = GameCommandType::StartGame;
        startCmd.randomSeed = 42;
        QVERIFY(engine.execute(startCmd).success);

        auto tripleFour = sameRankCards(Rank::Four, 3);
        auto humanCards = tripleFour;
        humanCards.push_back(Card::create(Rank::Five, Suit::Spades, 0));
        forcePlayingResponseState(engine, humanCards, sameRankCards(Rank::Three, 3));

        GameCommand play;
        play.type = GameCommandType::PlayCards;
        play.playerId = PlayerId::Player1;
        play.cardIds = idsOf(tripleFour);
        const auto result = engine.execute(play);

        QVERIFY2(result.success, "Three 4s must beat three 3s.");
        QCOMPARE(engine.state().fullState().lastPlayedBy, PlayerId::Player1);
        QCOMPARE(engine.state().fullState().lastPlayedCards.size(), size_t(3));
        QCOMPARE(engine.state().fullState().currentPlayer, PlayerId::Player2);
    }

    void testPairTwoCanBeatPairAceInPlayFlow() {
        GameEngine engine;
        GameCommand startCmd;
        startCmd.type = GameCommandType::StartGame;
        startCmd.randomSeed = 42;
        QVERIFY(engine.execute(startCmd).success);

        auto pairTwo = sameRankCards(Rank::Two, 2);
        auto humanCards = pairTwo;
        humanCards.push_back(Card::create(Rank::Three, Suit::Spades, 0));
        forcePlayingResponseState(engine, humanCards, sameRankCards(Rank::Ace, 2));

        GameCommand play;
        play.type = GameCommandType::PlayCards;
        play.playerId = PlayerId::Player1;
        play.cardIds = idsOf(pairTwo);
        const auto result = engine.execute(play);

        QVERIFY2(result.success, "Pair of 2s must beat pair of Aces.");
        QCOMPARE(engine.state().fullState().lastPlayedBy, PlayerId::Player1);
        QCOMPARE(engine.state().fullState().lastPlayedCards.size(), size_t(2));
        QCOMPARE(engine.state().fullState().currentPlayer, PlayerId::Player2);
    }

    void testSixAcesCanBeatFourQueensInPlayFlow() {
        GameEngine engine;
        GameCommand startCmd;
        startCmd.type = GameCommandType::StartGame;
        startCmd.randomSeed = 42;
        QVERIFY(engine.execute(startCmd).success);

        const auto sixAces = sameRankCards(Rank::Ace, 6);
        auto humanCards = sixAces;
        humanCards.push_back(Card::create(Rank::Three, Suit::Spades, 0));
        forcePlayingResponseState(engine, humanCards, sameRankCards(Rank::Queen, 4));

        GameCommand play;
        play.type = GameCommandType::PlayCards;
        play.playerId = PlayerId::Player1;
        play.cardIds = idsOf(sixAces);
        const auto result = engine.execute(play);

        QVERIFY2(result.success, "Six Aces must beat four Queens.");
        QCOMPARE(PatternAnalyzer::analyze(engine.state().fullState().lastPlayedCards).type,
                 CardPatternType::Rocket);
    }

    void testKingBombCanBeatHighestGunInPlayFlow() {
        GameEngine engine;
        GameCommand startCmd;
        startCmd.type = GameCommandType::StartGame;
        startCmd.randomSeed = 42;
        QVERIFY(engine.execute(startCmd).success);

        const std::vector<Card> kingBomb = {
            Card::create(Rank::SmallJoker, Suit::None, 0),
            Card::create(Rank::BigJoker, Suit::None, 0)
        };
        forcePlayingResponseState(engine, kingBomb, sameRankCards(Rank::Two, 4));

        GameCommand play;
        play.type = GameCommandType::PlayCards;
        play.playerId = PlayerId::Player1;
        play.cardIds = idsOf(kingBomb);
        const auto result = engine.execute(play);

        QVERIFY2(result.success, "King bomb must beat four Twos.");
        QCOMPARE(PatternAnalyzer::analyze(engine.state().fullState().lastPlayedCards).type,
                 CardPatternType::KingBomb);
    }

    void testKingBombCannotBeatFiveOfAKindInPlayFlow() {
        GameEngine engine;
        GameCommand startCmd;
        startCmd.type = GameCommandType::StartGame;
        startCmd.randomSeed = 42;
        QVERIFY(engine.execute(startCmd).success);

        const std::vector<Card> kingBomb = {
            Card::create(Rank::SmallJoker, Suit::None, 0),
            Card::create(Rank::BigJoker, Suit::None, 0)
        };
        forcePlayingResponseState(engine, kingBomb, sameRankCards(Rank::Three, 5));

        GameCommand play;
        play.type = GameCommandType::PlayCards;
        play.playerId = PlayerId::Player1;
        play.cardIds = idsOf(kingBomb);
        const auto result = engine.execute(play);

        QVERIFY(!result.success);
    }

    void testTripleSevenWithPairSixIsPlayable() {
        GameEngine engine;
        GameCommand startCmd;
        startCmd.type = GameCommandType::StartGame;
        startCmd.randomSeed = 42;
        QVERIFY(engine.execute(startCmd).success);

        auto cards = sameRankCards(Rank::Seven, 3);
        const auto pairSix = sameRankCards(Rank::Six, 2);
        cards.insert(cards.end(), pairSix.begin(), pairSix.end());
        forcePlayingResponseState(engine, cards, {});

        GameCommand play;
        play.type = GameCommandType::PlayCards;
        play.playerId = PlayerId::Player1;
        play.cardIds = idsOf(cards);
        const auto result = engine.execute(play);

        QVERIFY2(result.success, "Three Sevens with a pair of Sixes must be valid.");
        const auto pattern = PatternAnalyzer::analyze(engine.state().fullState().lastPlayedCards);
        QCOMPARE(pattern.type, CardPatternType::TripleWithPair);
        QCOMPARE(pattern.mainRank, Rank::Seven);
    }

    void testPairTwoCanBeatPairNineInPlayFlow() {
        GameEngine engine;
        GameCommand startCmd;
        startCmd.type = GameCommandType::StartGame;
        startCmd.randomSeed = 42;
        QVERIFY(engine.execute(startCmd).success);

        const auto pairTwo = sameRankCards(Rank::Two, 2);
        forcePlayingResponseState(engine, pairTwo, sameRankCards(Rank::Nine, 2));

        GameCommand play;
        play.type = GameCommandType::PlayCards;
        play.playerId = PlayerId::Player1;
        play.cardIds = idsOf(pairTwo);
        const auto result = engine.execute(play);

        QVERIFY2(result.success, "A pair of Twos must beat a pair of Nines.");
    }

    void testThreePassesResetTrickToLastPlayer() {
        GameEngine engine;
        GameCommand startCmd;
        startCmd.type = GameCommandType::StartGame;
        startCmd.randomSeed = 42;
        QVERIFY(engine.execute(startCmd).success);

        engine.state().setPhase(GamePhase::Playing);
        auto& fs = engine.state().fullState();
        fs.currentPlayer = PlayerId::Player2;
        fs.lastPlayedBy = PlayerId::Player1;
        fs.lastPlayedCards = sameRankCards(Rank::Three, 1);
        fs.consecutivePasses = 0;

        for (PlayerId player : {PlayerId::Player2, PlayerId::Player3, PlayerId::Player4}) {
            QCOMPARE(engine.state().fullState().currentPlayer, player);
            GameCommand pass;
            pass.type = GameCommandType::Pass;
            pass.playerId = player;
            const auto result = engine.execute(pass);
            QVERIFY(result.success);
        }

        QCOMPARE(engine.state().fullState().currentPlayer, PlayerId::Player1);
        QVERIFY(engine.state().fullState().lastPlayedCards.empty());
        QCOMPARE(engine.state().fullState().consecutivePasses, 0);
    }

    void testLeaderPassRequiresExplicitPermissionAndAdvancesTurn() {
        GameEngine engine;
        engine.state().setPhase(GamePhase::Playing);
        auto& fs = engine.state().fullState();
        fs.currentPlayer = PlayerId::Player1;
        fs.lastPlayedBy = PlayerId::Player1;
        fs.lastPlayedCards.clear();
        fs.consecutivePasses = 0;

        GameCommand pass;
        pass.type = GameCommandType::Pass;
        pass.playerId = PlayerId::Player1;

        const auto denied = engine.execute(pass);
        QVERIFY(!denied.success);
        QCOMPARE(denied.errorCode, ErrorCode::CannotPassAsLeader);
        QCOMPARE(fs.currentPlayer, PlayerId::Player1);

        pass.allowPassAsLeader = true;
        const auto allowed = engine.execute(pass);
        QVERIFY(allowed.success);
        QCOMPARE(fs.currentPlayer, PlayerId::Player2);
        QVERIFY(fs.lastPlayedCards.empty());
        QCOMPARE(fs.consecutivePasses, 0);
        QCOMPARE(allowed.events.size(), size_t(2));
        QCOMPARE(allowed.events[0].type, GameEventType::PlayerPassed);
        QCOMPARE(allowed.events[1].type, GameEventType::TurnChanged);
    }

};
QTEST_MAIN(TestGameEngine)
#include "test_game_engine.moc"
