#include <QtTest>
#include "core/rules/pattern_analyzer.h"
#include "core/model/deck.h"
using namespace fpdz;
class TestPatternAnalyzer : public QObject {
    Q_OBJECT
private slots:
    void testSingle() {
        std::vector<Card> cards = {Card::create(Rank::Five, Suit::Spades, 0)};
        auto p = PatternAnalyzer::analyze(cards);
        QCOMPARE(p.type, CardPatternType::Single);
    }
    void testPair() {
        std::vector<Card> cards = {
            Card::create(Rank::Five, Suit::Spades, 0),
            Card::create(Rank::Five, Suit::Hearts, 0)
        };
        auto p = PatternAnalyzer::analyze(cards);
        QCOMPARE(p.type, CardPatternType::Pair);
    }
    void testTriple() {
        std::vector<Card> cards = {
            Card::create(Rank::Five, Suit::Spades, 0),
            Card::create(Rank::Five, Suit::Hearts, 0),
            Card::create(Rank::Five, Suit::Clubs, 0)
        };
        auto p = PatternAnalyzer::analyze(cards);
        QCOMPARE(p.type, CardPatternType::Triple);
    }
    void testTripleWithSingleIsInvalid() {
        std::vector<Card> cards = {
            Card::create(Rank::Five, Suit::Spades, 0),
            Card::create(Rank::Five, Suit::Hearts, 0),
            Card::create(Rank::Five, Suit::Clubs, 0),
            Card::create(Rank::Three, Suit::Diamonds, 0)
        };
        auto p = PatternAnalyzer::analyze(cards);
        QCOMPARE(p.type, CardPatternType::Invalid);
    }
    void testTripleWithSingleIsValidOnlyInThreePlayerMode() {
        const std::vector<Card> cards = {
            Card::create(Rank::Five, Suit::Spades, 0),
            Card::create(Rank::Five, Suit::Hearts, 0),
            Card::create(Rank::Five, Suit::Clubs, 0),
            Card::create(Rank::Three, Suit::Diamonds, 0)
        };
        QCOMPARE(PatternAnalyzer::analyze(cards, THREE_PLAYER_COUNT).type,
                 CardPatternType::TripleWithSingle);
        QCOMPARE(PatternAnalyzer::analyze(cards, TWO_PLAYER_COUNT).type,
                 CardPatternType::TripleWithSingle);
        QCOMPARE(PatternAnalyzer::analyze(cards, PLAYER_COUNT).type,
                 CardPatternType::Invalid);
    }
    void testTripleWithPair() {
        std::vector<Card> cards = {
            Card::create(Rank::Five, Suit::Spades, 0),
            Card::create(Rank::Five, Suit::Hearts, 0),
            Card::create(Rank::Five, Suit::Clubs, 0),
            Card::create(Rank::Three, Suit::Diamonds, 0),
            Card::create(Rank::Three, Suit::Diamonds, 1)
        };
        auto p = PatternAnalyzer::analyze(cards);
        QCOMPARE(p.type, CardPatternType::TripleWithPair);
    }
    void testUserReportedTripleSevenWithPairSix() {
        std::vector<Card> cards = {
            Card::create(Rank::Seven, Suit::Spades, 0),
            Card::create(Rank::Seven, Suit::Hearts, 0),
            Card::create(Rank::Seven, Suit::Clubs, 0),
            Card::create(Rank::Six, Suit::Spades, 0),
            Card::create(Rank::Six, Suit::Hearts, 0)
        };
        const auto pattern = PatternAnalyzer::analyze(cards);
        QCOMPARE(pattern.type, CardPatternType::TripleWithPair);
        QCOMPARE(pattern.mainRank, Rank::Seven);
        QCOMPARE(pattern.totalCards, 5);
    }
    void testStraight() {
        std::vector<Card> cards = {
            Card::create(Rank::Three, Suit::Spades, 0),
            Card::create(Rank::Four, Suit::Hearts, 0),
            Card::create(Rank::Five, Suit::Clubs, 0),
            Card::create(Rank::Six, Suit::Diamonds, 0),
            Card::create(Rank::Seven, Suit::Spades, 0)
        };
        auto p = PatternAnalyzer::analyze(cards);
        QCOMPARE(p.type, CardPatternType::Straight);
        QCOMPARE(p.mainLength, 5);
    }
    void testConsecutivePairs() {
        std::vector<Card> cards = {
            Card::create(Rank::Three, Suit::Spades, 0), Card::create(Rank::Three, Suit::Hearts, 0),
            Card::create(Rank::Four, Suit::Clubs, 0), Card::create(Rank::Four, Suit::Diamonds, 0),
            Card::create(Rank::Five, Suit::Spades, 0), Card::create(Rank::Five, Suit::Hearts, 0)
        };
        auto p = PatternAnalyzer::analyze(cards);
        QCOMPARE(p.type, CardPatternType::ConsecutivePairs);
        QCOMPARE(p.mainLength, 3);
    }
    void testAirplaneWithPairs() {
        std::vector<Card> cards = {
            Card::create(Rank::Queen, Suit::Spades, 0),
            Card::create(Rank::Queen, Suit::Hearts, 0),
            Card::create(Rank::Queen, Suit::Clubs, 0),
            Card::create(Rank::King, Suit::Spades, 0),
            Card::create(Rank::King, Suit::Hearts, 0),
            Card::create(Rank::King, Suit::Clubs, 0),
            Card::create(Rank::Ace, Suit::Spades, 0),
            Card::create(Rank::Ace, Suit::Hearts, 0),
            Card::create(Rank::Ace, Suit::Clubs, 0),
            Card::create(Rank::Three, Suit::Spades, 0),
            Card::create(Rank::Three, Suit::Hearts, 0),
            Card::create(Rank::Four, Suit::Spades, 0),
            Card::create(Rank::Four, Suit::Hearts, 0),
            Card::create(Rank::Five, Suit::Spades, 0),
            Card::create(Rank::Five, Suit::Hearts, 0)
        };
        auto p = PatternAnalyzer::analyze(cards);
        QCOMPARE(p.type, CardPatternType::AirplaneWithPairs);
        QCOMPARE(p.mainLength, 3);
    }

    void testFourPlayerAirplaneRejectsPairWingMatchingBodyRank() {
        std::vector<Card> cards;
        const Suit suits[] = {Suit::Spades, Suit::Hearts, Suit::Clubs, Suit::Diamonds};
        for (int i = 0; i < 5; ++i) {
            cards.push_back(Card::create(Rank::Three, suits[i % 4],
                                         static_cast<DeckIndex>(i / 4)));
        }
        for (int i = 0; i < 3; ++i) {
            cards.push_back(Card::create(Rank::Four, suits[i], 0));
        }
        cards.push_back(Card::create(Rank::Two, Suit::Spades, 0));
        cards.push_back(Card::create(Rank::Two, Suit::Hearts, 0));

        QCOMPARE(PatternAnalyzer::analyze(cards, PLAYER_COUNT).type,
                 CardPatternType::Invalid);

        std::vector<Card> distinctWings;
        for (int i = 0; i < 3; ++i) {
            distinctWings.push_back(Card::create(Rank::Three, suits[i], 0));
            distinctWings.push_back(Card::create(Rank::Four, suits[i], 0));
        }
        distinctWings.push_back(Card::create(Rank::Five, Suit::Spades, 0));
        distinctWings.push_back(Card::create(Rank::Five, Suit::Hearts, 0));
        distinctWings.push_back(Card::create(Rank::Six, Suit::Spades, 0));
        distinctWings.push_back(Card::create(Rank::Six, Suit::Hearts, 0));
        const auto pattern = PatternAnalyzer::analyze(distinctWings, PLAYER_COUNT);
        QCOMPARE(pattern.type, CardPatternType::AirplaneWithPairs);
        QCOMPARE(pattern.mainRank, Rank::Three);
        QCOMPARE(pattern.mainLength, 2);
    }
    void testAirplaneWithSinglesIsInvalid() {
        std::vector<Card> cards = {
            Card::create(Rank::Three, Suit::Spades, 0),
            Card::create(Rank::Three, Suit::Hearts, 0),
            Card::create(Rank::Three, Suit::Clubs, 0),
            Card::create(Rank::Four, Suit::Spades, 0),
            Card::create(Rank::Four, Suit::Hearts, 0),
            Card::create(Rank::Four, Suit::Clubs, 0),
            Card::create(Rank::Seven, Suit::Spades, 0),
            Card::create(Rank::Eight, Suit::Hearts, 0)
        };
        auto p = PatternAnalyzer::analyze(cards);
        QCOMPARE(p.type, CardPatternType::Invalid);
    }
    void testAirplaneWithSinglesIsValidOnlyInThreePlayerMode() {
        const std::vector<Card> cards = {
            Card::create(Rank::Three, Suit::Spades, 0),
            Card::create(Rank::Three, Suit::Hearts, 0),
            Card::create(Rank::Three, Suit::Clubs, 0),
            Card::create(Rank::Four, Suit::Spades, 0),
            Card::create(Rank::Four, Suit::Hearts, 0),
            Card::create(Rank::Four, Suit::Clubs, 0),
            Card::create(Rank::Seven, Suit::Spades, 0),
            Card::create(Rank::Eight, Suit::Hearts, 0)
        };
        const auto pattern = PatternAnalyzer::analyze(cards, THREE_PLAYER_COUNT);
        QCOMPARE(pattern.type, CardPatternType::AirplaneWithSingles);
        QCOMPARE(pattern.mainLength, 2);
        QCOMPARE(PatternAnalyzer::analyze(cards, TWO_PLAYER_COUNT).type,
                 CardPatternType::AirplaneWithSingles);
        QCOMPARE(PatternAnalyzer::analyze(cards, PLAYER_COUNT).type,
                 CardPatternType::Invalid);
    }
    void testFourWithTwoIsInvalid() {
        std::vector<Card> cards = {
            Card::create(Rank::Eight, Suit::Spades, 0),
            Card::create(Rank::Eight, Suit::Hearts, 0),
            Card::create(Rank::Eight, Suit::Clubs, 0),
            Card::create(Rank::Eight, Suit::Diamonds, 0),
            Card::create(Rank::Three, Suit::Spades, 0),
            Card::create(Rank::Four, Suit::Hearts, 0)
        };
        auto p = PatternAnalyzer::analyze(cards);
        QCOMPARE(p.type, CardPatternType::Invalid);
    }
    void testFourWithTwoVariantsAreValidOnlyInThreePlayerMode() {
        const std::vector<Card> singles = {
            Card::create(Rank::Eight, Suit::Spades, 0),
            Card::create(Rank::Eight, Suit::Hearts, 0),
            Card::create(Rank::Eight, Suit::Clubs, 0),
            Card::create(Rank::Eight, Suit::Diamonds, 0),
            Card::create(Rank::Three, Suit::Spades, 0),
            Card::create(Rank::Four, Suit::Hearts, 0)
        };
        QCOMPARE(PatternAnalyzer::analyze(singles, THREE_PLAYER_COUNT).type,
                 CardPatternType::FourWithTwoSingles);
        QCOMPARE(PatternAnalyzer::analyze(singles, TWO_PLAYER_COUNT).type,
                 CardPatternType::FourWithTwoSingles);
        QCOMPARE(PatternAnalyzer::analyze(singles, PLAYER_COUNT).type,
                 CardPatternType::Invalid);

        auto pairs = std::vector<Card>(singles.begin(), singles.begin() + 4);
        pairs.push_back(Card::create(Rank::Three, Suit::Spades, 0));
        pairs.push_back(Card::create(Rank::Three, Suit::Hearts, 0));
        pairs.push_back(Card::create(Rank::Four, Suit::Spades, 0));
        pairs.push_back(Card::create(Rank::Four, Suit::Hearts, 0));
        QCOMPARE(PatternAnalyzer::analyze(pairs, THREE_PLAYER_COUNT).type,
                 CardPatternType::FourWithTwoPairs);
        QCOMPARE(PatternAnalyzer::analyze(pairs, TWO_PLAYER_COUNT).type,
                 CardPatternType::FourWithTwoPairs);
        QCOMPARE(PatternAnalyzer::analyze(pairs, PLAYER_COUNT).type,
                 CardPatternType::Invalid);
    }
    void testGun() {
        std::vector<Card> cards = {
            Card::create(Rank::Five, Suit::Spades, 0),
            Card::create(Rank::Five, Suit::Hearts, 0),
            Card::create(Rank::Five, Suit::Clubs, 0),
            Card::create(Rank::Five, Suit::Diamonds, 0)
        };
        auto p = PatternAnalyzer::analyze(cards);
        QCOMPARE(p.type, CardPatternType::Gun);
        QVERIFY(p.isBomb());
    }
    void testHeavenlyLord() {
        std::vector<Card> cards = {
            Card::create(Rank::SmallJoker, Suit::None, 0),
            Card::create(Rank::SmallJoker, Suit::None, 1),
            Card::create(Rank::BigJoker, Suit::None, 0),
            Card::create(Rank::BigJoker, Suit::None, 1)
        };
        auto p = PatternAnalyzer::analyze(cards);
        QCOMPARE(p.type, CardPatternType::HeavenlyLord);
    }

    void testKingBomb() {
        std::vector<Card> cards = {
            Card::create(Rank::SmallJoker, Suit::None, 0),
            Card::create(Rank::BigJoker, Suit::None, 0)
        };
        const auto pattern = PatternAnalyzer::analyze(cards);
        QCOMPARE(pattern.type, CardPatternType::KingBomb);
        QCOMPARE(pattern.totalCards, 2);
        QVERIFY(pattern.isBomb());
    }
    void testInvalidPattern() {
        std::vector<Card> cards = {
            Card::create(Rank::Three, Suit::Spades, 0),
            Card::create(Rank::Five, Suit::Hearts, 0)
        };
        auto p = PatternAnalyzer::analyze(cards);
        QCOMPARE(p.type, CardPatternType::Invalid);
    }
    void testStraightCannotIncludeTwo() {
        std::vector<Card> cards = {
            Card::create(Rank::Ten, Suit::Spades, 0),
            Card::create(Rank::Jack, Suit::Hearts, 0),
            Card::create(Rank::Queen, Suit::Clubs, 0),
            Card::create(Rank::King, Suit::Diamonds, 0),
            Card::create(Rank::Two, Suit::Spades, 0)
        };
        auto p = PatternAnalyzer::analyze(cards);
        QVERIFY(p.type != CardPatternType::Straight);
    }
    void testConsecutivePairsThroughAce() {
        std::vector<Card> cards;
        for (Rank rank : {Rank::Queen, Rank::King, Rank::Ace}) {
            cards.push_back(Card::create(rank, Suit::Spades, 0));
            cards.push_back(Card::create(rank, Suit::Hearts, 0));
        }
        QCOMPARE(PatternAnalyzer::analyze(cards).type, CardPatternType::ConsecutivePairs);
    }
    void testTriplesThroughAce() {
        std::vector<Card> cards;
        for (Rank rank : {Rank::Queen, Rank::King, Rank::Ace}) {
            cards.push_back(Card::create(rank, Suit::Spades, 0));
            cards.push_back(Card::create(rank, Suit::Hearts, 0));
            cards.push_back(Card::create(rank, Suit::Clubs, 0));
        }
        QCOMPARE(PatternAnalyzer::analyze(cards).type, CardPatternType::Airplane);
    }
};
QTEST_MAIN(TestPatternAnalyzer)
#include "test_pattern_analyzer.moc"
