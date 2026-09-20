#include <QtTest>
#include "core/rules/scoring_engine.h"
using namespace fpdz;
class TestScoringEngine : public QObject {
    Q_OBJECT
private slots:
    void testMultiplierCap() {
        QCOMPARE(ScoringEngine::applyMultiplierCap(8192), static_cast<int64_t>(4096));
        QCOMPARE(ScoringEngine::applyMultiplierCap(100), static_cast<int64_t>(100));
    }
    void testZeroSum() {
        ScoreResult r;
        r.scoreChanges = {100, -33, -33, -34};
        QVERIFY(r.isZeroSum());
    }
    void testThreePlayerLandlordSettlementIsZeroSum() {
        FullGameState state;
        state.activePlayerCount = THREE_PLAYER_COUNT;
        state.baseScore = 2;
        state.currentMultiplier = 1;
        state.players[0].role = Role::Landlord;
        state.players[1].role = Role::Farmer;
        state.players[2].role = Role::Farmer;
        state.players[1].hasPlayedThisRound = true;

        const auto result = ScoringEngine::calculate(state);
        QVERIFY(result.landlordWon);
        QCOMPARE(result.scoreChanges[0], int64_t(4));
        QCOMPARE(result.scoreChanges[1], int64_t(-2));
        QCOMPARE(result.scoreChanges[2], int64_t(-2));
        QCOMPARE(result.scoreChanges[3], int64_t(0));
        QVERIFY(result.isZeroSum());
    }
    void testTwoPlayerSettlementIsZeroSum() {
        FullGameState state;
        state.activePlayerCount = TWO_PLAYER_COUNT;
        state.baseScore = 2;
        state.currentMultiplier = 2;
        state.players[0].role = Role::Landlord;
        state.players[1].role = Role::Farmer;
        state.players[0].hand.addCard(Card::create(Rank::Three, Suit::Spades, 0));
        state.players[0].hasPlayedThisRound = true;
        state.players[0].cardsPlayedCount = 2;

        const auto result = ScoringEngine::calculate(state);
        QVERIFY(!result.landlordWon);
        QCOMPARE(result.scoreChanges[0], int64_t(-4));
        QCOMPARE(result.scoreChanges[1], int64_t(4));
        QCOMPARE(result.scoreChanges[2], int64_t(0));
        QCOMPARE(result.scoreChanges[3], int64_t(0));
        QVERIFY(result.isZeroSum());
    }
};
QTEST_MAIN(TestScoringEngine)
#include "test_scoring_engine.moc"
