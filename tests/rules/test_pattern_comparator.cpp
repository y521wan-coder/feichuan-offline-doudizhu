#include <QtTest>
#include "core/rules/pattern_comparator.h"
#include "core/rules/pattern_analyzer.h"

using namespace fpdz;

namespace {

CardPattern makePattern(CardPatternType type, Rank rank, int mainLength, int totalCards) {
    CardPattern p;
    p.type = type;
    p.mainRank = rank;
    p.mainLength = mainLength;
    p.totalCards = totalCards;
    return p;
}

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

CardPattern analyzeSameRank(Rank rank, int count) {
    return PatternAnalyzer::analyze(sameRankCards(rank, count));
}

}

class TestPatternComparator : public QObject {
    Q_OBJECT
private slots:
    void testBombBeatsNonBomb() {
        CardPattern bomb;
        bomb.type = CardPatternType::Gun;
        bomb.mainRank = Rank::Three;
        bomb.mainLength = 4;
        bomb.totalCards = 4;
        CardPattern single;
        single.type = CardPatternType::Single;
        single.mainRank = Rank::BigJoker;
        single.mainLength = 1;
        single.totalCards = 1;
        QVERIFY(PatternComparator::canBeat(bomb, single));
        QVERIFY(!PatternComparator::canBeat(single, bomb));
    }

    void testHigherBombBeatsLower() {
        CardPattern gun;
        gun.type = CardPatternType::Gun;
        gun.mainRank = Rank::Ace;
        gun.mainLength = 4;
        gun.totalCards = 4;
        CardPattern cannon;
        cannon.type = CardPatternType::Cannon;
        cannon.mainRank = Rank::Three;
        cannon.mainLength = 5;
        cannon.totalCards = 5;
        QVERIFY(PatternComparator::canBeat(cannon, gun));
        QVERIFY(!PatternComparator::canBeat(gun, cannon));
    }

    void testSameBombTypeUsesRank() {
        CardPattern low;
        low.type = CardPatternType::Gun;
        low.mainRank = Rank::Nine;
        low.mainLength = 4;
        low.totalCards = 4;
        CardPattern high = low;
        high.mainRank = Rank::Ace;
        QVERIFY(PatternComparator::canBeat(high, low));
        QVERIFY(!PatternComparator::canBeat(low, high));
    }

    void testHigherSingleBeatsLower() {
        CardPattern a, b;
        a.type = CardPatternType::Single;
        a.mainRank = Rank::Ace;
        a.mainLength = 1;
        a.totalCards = 1;
        b.type = CardPatternType::Single;
        b.mainRank = Rank::Three;
        b.mainLength = 1;
        b.totalCards = 1;
        QVERIFY(PatternComparator::canBeat(a, b));
        QVERIFY(!PatternComparator::canBeat(b, a));
    }

    void testDifferentTypeCannotBeat() {
        CardPattern pair, triple;
        pair.type = CardPatternType::Pair;
        pair.mainRank = Rank::Ace;
        pair.mainLength = 1;
        pair.totalCards = 2;
        triple.type = CardPatternType::Triple;
        triple.mainRank = Rank::Three;
        triple.mainLength = 1;
        triple.totalCards = 3;
        QVERIFY(!PatternComparator::canBeat(pair, triple));
    }

    void testUserReportedPairAndTripleResponses() {
        const auto tripleThree = analyzeSameRank(Rank::Three, 3);
        const auto tripleFour = analyzeSameRank(Rank::Four, 3);
        const auto pairAce = analyzeSameRank(Rank::Ace, 2);
        const auto pairTwo = analyzeSameRank(Rank::Two, 2);

        QCOMPARE(tripleThree.type, CardPatternType::Triple);
        QCOMPARE(tripleFour.type, CardPatternType::Triple);
        QCOMPARE(pairAce.type, CardPatternType::Pair);
        QCOMPARE(pairTwo.type, CardPatternType::Pair);
        QVERIFY(PatternComparator::canBeat(tripleFour, tripleThree));
        QVERIFY(!PatternComparator::canBeat(tripleThree, tripleFour));
        QVERIFY(PatternComparator::canBeat(pairTwo, pairAce));
        QVERIFY(!PatternComparator::canBeat(pairAce, pairTwo));
    }

    void testNonBombSameTypeSameLengthUsesMainRank() {
        QVERIFY(PatternComparator::canBeat(
            makePattern(CardPatternType::Single, Rank::Two, 1, 1),
            makePattern(CardPatternType::Single, Rank::Ace, 1, 1)));
        QVERIFY(PatternComparator::canBeat(
            makePattern(CardPatternType::TripleWithPair, Rank::Four, 1, 5),
            makePattern(CardPatternType::TripleWithPair, Rank::Three, 1, 5)));
        QVERIFY(PatternComparator::canBeat(
            makePattern(CardPatternType::Straight, Rank::Four, 5, 5),
            makePattern(CardPatternType::Straight, Rank::Three, 5, 5)));
        QVERIFY(PatternComparator::canBeat(
            makePattern(CardPatternType::ConsecutivePairs, Rank::Queen, 3, 6),
            makePattern(CardPatternType::ConsecutivePairs, Rank::Jack, 3, 6)));
        QVERIFY(PatternComparator::canBeat(
            makePattern(CardPatternType::Airplane, Rank::Queen, 3, 9),
            makePattern(CardPatternType::Airplane, Rank::Jack, 3, 9)));
        QVERIFY(PatternComparator::canBeat(
            makePattern(CardPatternType::AirplaneWithPairs, Rank::Four, 2, 10),
            makePattern(CardPatternType::AirplaneWithPairs, Rank::Three, 2, 10)));
    }

    void testNonBombDifferentLengthCannotBeat() {
        QVERIFY(!PatternComparator::canBeat(
            makePattern(CardPatternType::Straight, Rank::Four, 6, 6),
            makePattern(CardPatternType::Straight, Rank::Three, 5, 5)));
        QVERIFY(!PatternComparator::canBeat(
            makePattern(CardPatternType::ConsecutivePairs, Rank::Four, 4, 8),
            makePattern(CardPatternType::ConsecutivePairs, Rank::Three, 3, 6)));
        QVERIFY(!PatternComparator::canBeat(
            makePattern(CardPatternType::AirplaneWithPairs, Rank::Four, 3, 15),
            makePattern(CardPatternType::AirplaneWithPairs, Rank::Three, 2, 10)));
    }

    void testBombHierarchyAndSameBombRank() {
        const auto kingBomb = makePattern(CardPatternType::KingBomb, Rank::BigJoker, 2, 2);
        const auto doubleKingBomb = makePattern(CardPatternType::HeavenlyLord, Rank::BigJoker, 4, 4);
        QVERIFY(PatternComparator::canBeat(
            analyzeSameRank(Rank::Four, 8),
            analyzeSameRank(Rank::Three, 8)));
        QVERIFY(PatternComparator::canBeat(
            analyzeSameRank(Rank::Three, 5),
            analyzeSameRank(Rank::Ace, 4)));
        QVERIFY(PatternComparator::canBeat(
            analyzeSameRank(Rank::Two, 4),
            analyzeSameRank(Rank::Ace, 4)));
        QVERIFY(PatternComparator::canBeat(kingBomb, analyzeSameRank(Rank::Two, 4)));
        QVERIFY(!PatternComparator::canBeat(kingBomb, analyzeSameRank(Rank::Three, 5)));
        QVERIFY(PatternComparator::canBeat(analyzeSameRank(Rank::Three, 5), kingBomb));
        QVERIFY(PatternComparator::canBeat(doubleKingBomb, kingBomb));
        QVERIFY(!PatternComparator::canBeat(kingBomb, doubleKingBomb));
        QVERIFY(!PatternComparator::canBeat(kingBomb, kingBomb));
        QVERIFY(PatternComparator::canBeat(
            doubleKingBomb,
            analyzeSameRank(Rank::Two, 8)));
        QVERIFY(!PatternComparator::canBeat(
            analyzeSameRank(Rank::Two, 8),
            doubleKingBomb));
    }
};

QTEST_MAIN(TestPatternComparator)
#include "test_pattern_comparator.moc"
