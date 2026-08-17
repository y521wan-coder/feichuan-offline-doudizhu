#include <QtTest>

#include "core/model/card.h"
#include "core/rules/pattern_analyzer.h"
#include "core/text/card_text_formatter.h"

using namespace fpdz;

class TestCardTextFormatter : public QObject {
    Q_OBJECT

private slots:
    void testKingBombNames() {
        CardPattern kingBomb;
        kingBomb.type = CardPatternType::KingBomb;
        QCOMPARE(CardTextFormatter::formatPlayedCards(kingBomb, {}), std::wstring(L"王炸"));

        CardPattern doubleKingBomb;
        doubleKingBomb.type = CardPatternType::HeavenlyLord;
        QCOMPARE(CardTextFormatter::formatPlayedCards(doubleKingBomb, {}), std::wstring(L"天尊"));
    }

    void testRankSpeechUsesWanerbaNames() {
        QCOMPARE(QString::fromStdWString(CardTextFormatter::formatRankSpeech(Rank::Jack)),
                 QString::fromUtf8(u8"钩"));
        QCOMPARE(QString::fromStdWString(CardTextFormatter::formatRankSpeech(Rank::Queen)),
                 QString::fromUtf8(u8"圈"));
        QCOMPARE(QString::fromStdWString(CardTextFormatter::formatRankSpeech(Rank::King)),
                 QString::fromUtf8(u8"k"));
        QCOMPARE(QString::fromStdWString(CardTextFormatter::formatRankSpeech(Rank::Ace)),
                 QString::fromUtf8(u8"尖"));

        QCOMPARE(QString::fromStdWString(
                     CardTextFormatter::formatSameRankSpeech(Rank::Ace, 2)),
                 QString::fromUtf8(u8"对尖"));
    }

    void testSameRankSpeechContainsNoSuit() {
        const auto text = QString::fromStdWString(
            CardTextFormatter::formatSameRankSpeech(Rank::Seven, 3));
        QCOMPARE(text, QString::fromUtf8(u8"3张7"));
        QVERIFY(!containsSuit(text));
    }

    void testCardsSpeechGroupsByRankWithoutSuit() {
        const std::vector<Card> cards = {
            Card::create(Rank::King, Suit::Spades, 0),
            Card::create(Rank::King, Suit::Hearts, 0),
            Card::create(Rank::Jack, Suit::Diamonds, 1)
        };

        const auto text = QString::fromStdWString(CardTextFormatter::formatCards(cards));
        QCOMPARE(text, QString::fromUtf8(u8"对k、1张钩"));
        QVERIFY(!containsSuit(text));
    }

    void testPublicCardsOmitOnlySingleCardCounts() {
        const std::vector<Card> cards = {
            Card::create(Rank::Ten, Suit::Spades, 0),
            Card::create(Rank::Eight, Suit::Spades, 0),
            Card::create(Rank::Eight, Suit::Hearts, 0),
            Card::create(Rank::Eight, Suit::Clubs, 0),
            Card::create(Rank::King, Suit::Diamonds, 0),
            Card::create(Rank::SmallJoker, Suit::None, 0),
            Card::create(Rank::Five, Suit::Hearts, 0),
            Card::create(Rank::Queen, Suit::Clubs, 0)
        };

        QCOMPARE(QString::fromStdWString(CardTextFormatter::formatPublicCards(cards)),
                 QString::fromUtf8(u8"10、3张8、k、小王、5、圈"));
        QCOMPARE(QString::fromStdWString(CardTextFormatter::formatCards(cards)),
                 QString::fromUtf8(u8"1张10、3张8、1张k、1张小王、1张5、1张圈"));
    }

    void testTripleWithPairSpeechIncludesAllCards() {
        const std::vector<Card> cards = {
            Card::create(Rank::Two, Suit::Spades, 0),
            Card::create(Rank::Two, Suit::Hearts, 0),
            Card::create(Rank::Two, Suit::Clubs, 0),
            Card::create(Rank::Three, Suit::Spades, 0),
            Card::create(Rank::Three, Suit::Hearts, 0)
        };
        const auto pattern = PatternAnalyzer::analyze(cards);
        QVERIFY(pattern.isValid());
        QCOMPARE(QString::fromStdWString(CardTextFormatter::formatPlayedCards(pattern, cards)),
                 QString::fromUtf8(u8"三个2，带对3"));
    }

    void testSequenceAndAirplaneSpeechIncludesEveryComponent() {
        std::vector<Card> pairs;
        for (const auto rank : {Rank::Three, Rank::Four, Rank::Five}) {
            pairs.push_back(Card::create(rank, Suit::Spades, 0));
            pairs.push_back(Card::create(rank, Suit::Hearts, 0));
        }
        const auto pairPattern = PatternAnalyzer::analyze(pairs);
        QCOMPARE(QString::fromStdWString(
                     CardTextFormatter::formatPlayedCards(pairPattern, pairs)),
                 QString::fromUtf8(u8"3、4、5，连对"));

        std::vector<Card> airplane;
        for (int i = 0; i < 5; ++i) {
            airplane.push_back(Card::create(Rank::Three,
                static_cast<Suit>(i % 4), static_cast<DeckIndex>(i / 4)));
        }
        for (int i = 0; i < 3; ++i) {
            airplane.push_back(Card::create(Rank::Four, static_cast<Suit>(i), 0));
        }
        airplane.push_back(Card::create(Rank::Two, Suit::Spades, 0));
        airplane.push_back(Card::create(Rank::Two, Suit::Hearts, 0));
        const auto airplanePattern = PatternAnalyzer::analyze(airplane);
        QCOMPARE(QString::fromStdWString(
                     CardTextFormatter::formatPlayedCards(airplanePattern, airplane)),
                 QString::fromUtf8(u8"3、4，飞机，带对2、对3"));
    }

    void testCardWithSelectionIgnoresSelectionAndPosition() {
        const auto card = Card::create(Rank::Four, Suit::Clubs, 1);
        const auto text = QString::fromStdWString(
            CardTextFormatter::formatCardWithSelection(card, true, 3, 20, 2));
        QCOMPARE(text, QString::fromUtf8(u8"对4"));
        QVERIFY(!text.contains(QString::fromUtf8(u8"选中")));
        QVERIFY(!containsSuit(text));
    }

    void testBombSpeechUsesConciseCountRankAndName() {
        CardPattern pattern;
        pattern.type = CardPatternType::Cannon;
        pattern.mainRank = Rank::Seven;
        const auto text = CardTextFormatter::formatPlayedCards(pattern, {});
        QCOMPARE(QString::fromStdWString(text), QString::fromUtf8(u8"五个七，炮"));

        pattern.type = CardPatternType::Gun;
        pattern.mainRank = Rank::Three;
        QCOMPARE(QString::fromStdWString(CardTextFormatter::formatPlayedCards(pattern, {})),
                 QString::fromUtf8(u8"四个三，枪"));
    }

private:
    static bool containsSuit(const QString& text) {
        return text.contains(QString::fromUtf8(u8"黑桃")) ||
               text.contains(QString::fromUtf8(u8"红心")) ||
               text.contains(QString::fromUtf8(u8"梅花")) ||
               text.contains(QString::fromUtf8(u8"方块"));
    }
};

QTEST_MAIN(TestCardTextFormatter)
#include "test_card_text_formatter.moc"
