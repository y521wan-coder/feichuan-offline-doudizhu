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

    void testConsecutivePairsSpeechUsesEndpoints() {
        std::vector<Card> pairs;
        for (const auto rank : {Rank::Three, Rank::Four, Rank::Five}) {
            pairs.push_back(Card::create(rank, Suit::Spades, 0));
            pairs.push_back(Card::create(rank, Suit::Hearts, 0));
        }
        const auto pairPattern = PatternAnalyzer::analyze(pairs);
        QCOMPARE(pairPattern.type, CardPatternType::ConsecutivePairs);
        QCOMPARE(pairPattern.mainRank, Rank::Three);
        QCOMPARE(pairPattern.mainLength, 3);
        QCOMPARE(QString::fromStdWString(
                     CardTextFormatter::formatPlayedCards(pairPattern, pairs)),
                 QString::fromUtf8(u8"3到5连对"));
    }

    void testAirplaneWithPairsSpeechKeepsEveryComponent() {
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
        QCOMPARE(airplanePattern.type, CardPatternType::AirplaneWithPairs);
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

    void testStraightSpeechUsesEndpoints() {
        const auto five = sequenceOf(Rank::Three, 5);
        const auto fivePattern = PatternAnalyzer::analyze(five);
        QCOMPARE(fivePattern.type, CardPatternType::Straight);
        QCOMPARE(fivePattern.mainRank, Rank::Three);
        QCOMPARE(fivePattern.mainLength, 5);
        QCOMPARE(QString::fromStdWString(
                     CardTextFormatter::formatPlayedCards(fivePattern, five)),
                 QString::fromUtf8(u8"3到7顺子"));

        const auto seven = sequenceOf(Rank::Three, 7);
        const auto sevenPattern = PatternAnalyzer::analyze(seven);
        QCOMPARE(sevenPattern.type, CardPatternType::Straight);
        QCOMPARE(sevenPattern.mainLength, 7);
        QCOMPARE(QString::fromStdWString(
                     CardTextFormatter::formatPlayedCards(sevenPattern, seven)),
                 QString::fromUtf8(u8"3到9顺子"));

        const auto high = sequenceOf(Rank::Ten, 5);
        const auto highPattern = PatternAnalyzer::analyze(high);
        QCOMPARE(highPattern.type, CardPatternType::Straight);
        QCOMPARE(highPattern.mainRank, Rank::Ten);
        QCOMPARE(highPattern.mainLength, 5);
        QCOMPARE(QString::fromStdWString(
                     CardTextFormatter::formatPlayedCards(highPattern, high)),
                 QString::fromUtf8(u8"10到尖顺子"));
    }

    void testConsecutivePairSpeechUsesEndpoints() {
        const auto lowPairs = sequenceOf(Rank::Four, 3, 2);
        const auto lowPattern = PatternAnalyzer::analyze(lowPairs);
        QCOMPARE(lowPattern.type, CardPatternType::ConsecutivePairs);
        QCOMPARE(lowPattern.mainRank, Rank::Four);
        QCOMPARE(lowPattern.mainLength, 3);
        QCOMPARE(QString::fromStdWString(
                     CardTextFormatter::formatPlayedCards(lowPattern, lowPairs)),
                 QString::fromUtf8(u8"4到6连对"));

        const auto jackPairs = sequenceOf(Rank::Jack, 3, 2);
        const auto jackPattern = PatternAnalyzer::analyze(jackPairs);
        QCOMPARE(jackPattern.type, CardPatternType::ConsecutivePairs);
        QCOMPARE(jackPattern.mainRank, Rank::Jack);
        QCOMPARE(jackPattern.mainLength, 3);
        QCOMPARE(QString::fromStdWString(
                     CardTextFormatter::formatPlayedCards(jackPattern, jackPairs)),
                 QString::fromUtf8(u8"钩到k连对"));
    }

    void testPlainAirplaneSpeechUsesEndpoints() {
        const auto twoTriples = sequenceOf(Rank::Three, 2, 3);
        const auto twoPattern = PatternAnalyzer::analyze(twoTriples);
        QCOMPARE(twoPattern.type, CardPatternType::Airplane);
        QCOMPARE(twoPattern.mainRank, Rank::Three);
        QCOMPARE(twoPattern.mainLength, 2);
        QCOMPARE(QString::fromStdWString(
                     CardTextFormatter::formatPlayedCards(twoPattern, twoTriples)),
                 QString::fromUtf8(u8"3到4飞机"));

        const auto fourTriples = sequenceOf(Rank::Three, 4, 3);
        const auto fourPattern = PatternAnalyzer::analyze(fourTriples);
        QCOMPARE(fourPattern.type, CardPatternType::Airplane);
        QCOMPARE(fourPattern.mainRank, Rank::Three);
        QCOMPARE(fourPattern.mainLength, 4);
        QCOMPARE(QString::fromStdWString(
                     CardTextFormatter::formatPlayedCards(fourPattern, fourTriples)),
                 QString::fromUtf8(u8"3到6飞机"));

        const auto jackTriples = sequenceOf(Rank::Jack, 2, 3);
        const auto jackPattern = PatternAnalyzer::analyze(jackTriples);
        QCOMPARE(jackPattern.type, CardPatternType::Airplane);
        QCOMPARE(jackPattern.mainRank, Rank::Jack);
        QCOMPARE(jackPattern.mainLength, 2);
        QCOMPARE(QString::fromStdWString(
                     CardTextFormatter::formatPlayedCards(jackPattern, jackTriples)),
                 QString::fromUtf8(u8"钩到圈飞机"));
    }

    void testSinglePairTripleAndBombSpeechStayUnchanged() {
        const auto single = sameRank(Rank::Nine, 1);
        QCOMPARE(QString::fromStdWString(CardTextFormatter::formatPlayedCards(
                     PatternAnalyzer::analyze(single), single)),
                 QString::fromUtf8(u8"1张9"));

        const auto pair = sameRank(Rank::Nine, 2);
        QCOMPARE(QString::fromStdWString(CardTextFormatter::formatPlayedCards(
                     PatternAnalyzer::analyze(pair), pair)),
                 QString::fromUtf8(u8"对9"));

        const auto triple = sameRank(Rank::Nine, 3);
        QCOMPARE(QString::fromStdWString(CardTextFormatter::formatPlayedCards(
                     PatternAnalyzer::analyze(triple), triple)),
                 QString::fromUtf8(u8"三个9"));

        const wchar_t* const bombNames[] = {L"枪", L"炮", L"火箭", L"导弹", L"天炸"};
        const wchar_t* const bombCounts[] = {L"四个", L"五个", L"六个", L"七个", L"八个"};
        for (int index = 0; index < 5; ++index) {
            const auto bomb = sameRank(Rank::Nine, index + 4);
            const auto pattern = PatternAnalyzer::analyze(bomb);
            QCOMPARE(pattern.totalCards, index + 4);
            // 炸弹文字使用读屏用中文点数，而不是阿拉伯数字。
            const std::wstring expected =
                std::wstring(bombCounts[index]) + L"九，" + bombNames[index];
            QCOMPARE(CardTextFormatter::formatPlayedCards(pattern, bomb), expected);
        }
    }

    void testFormatCardsKeepsEveryRankForHandQuery() {
        const auto straight = sequenceOf(Rank::Three, 5);
        const auto straightText = QString::fromStdWString(CardTextFormatter::formatCards(straight));
        QCOMPARE(straightText, QString::fromUtf8(u8"1张3、1张4、1张5、1张6、1张7"));
        QVERIFY(!straightText.contains(QString::fromUtf8(u8"到")));
        QCOMPARE(QString::fromStdWString(
                     CardTextFormatter::formatCards(sequenceOf(Rank::Three, 4, 3))),
                 QString::fromUtf8(u8"3张3、3张4、3张5、3张6"));
    }

    void testFormatPublicCardsKeepsBottomCardFormat() {
        std::vector<Card> bottom = sameRank(Rank::Three, 2);
        const auto four = sameRank(Rank::Four, 1);
        bottom.insert(bottom.end(), four.begin(), four.end());
        const auto joker = sameRank(Rank::BigJoker, 1);
        bottom.insert(bottom.end(), joker.begin(), joker.end());
        const auto publicText = QString::fromStdWString(CardTextFormatter::formatPublicCards(bottom));
        QCOMPARE(publicText, QString::fromUtf8(u8"对3、4、大王"));
        QVERIFY(!publicText.contains(QString::fromUtf8(u8"到")));
        QCOMPARE(QString::fromStdWString(CardTextFormatter::formatCards(bottom)),
                 QString::fromUtf8(u8"对3、1张4、1张大王"));
    }

    void testEndpointRangeSpeechWorksWithoutCards() {
        CardPattern pattern;
        pattern.type = CardPatternType::Straight;
        pattern.mainRank = Rank::Three;
        pattern.mainLength = 5;
        pattern.totalCards = 5;
        QCOMPARE(QString::fromStdWString(CardTextFormatter::formatPlayedCards(pattern, {})),
                 QString::fromUtf8(u8"3到7顺子"));

        pattern.type = CardPatternType::ConsecutivePairs;
        pattern.mainRank = Rank::Four;
        pattern.mainLength = 3;
        pattern.totalCards = 6;
        QCOMPARE(QString::fromStdWString(CardTextFormatter::formatPlayedCards(pattern, {})),
                 QString::fromUtf8(u8"4到6连对"));

        pattern.type = CardPatternType::Airplane;
        pattern.mainRank = Rank::Three;
        pattern.mainLength = 4;
        pattern.totalCards = 12;
        QCOMPARE(QString::fromStdWString(CardTextFormatter::formatPlayedCards(pattern, {})),
                 QString::fromUtf8(u8"3到6飞机"));
    }

    void testInvalidSequenceRangeFallsBackToEnumeration() {
        CardPattern pattern;
        pattern.type = CardPatternType::Straight;
        pattern.mainRank = Rank::King;
        pattern.mainLength = 5;
        QString text = QString::fromStdWString(CardTextFormatter::formatPlayedCards(pattern, {}));
        QVERIFY(!text.contains(QString::fromUtf8(u8"到")));
        QVERIFY(text.contains(QString::fromUtf8(u8"顺子")));

        pattern.mainRank = Rank::Three;
        pattern.mainLength = 3;
        text = QString::fromStdWString(CardTextFormatter::formatPlayedCards(pattern, {}));
        QVERIFY(!text.contains(QString::fromUtf8(u8"到")));
        QVERIFY(text.contains(QString::fromUtf8(u8"顺子")));

        pattern.type = CardPatternType::ConsecutivePairs;
        pattern.mainRank = Rank::King;
        pattern.mainLength = 3;
        text = QString::fromStdWString(CardTextFormatter::formatPlayedCards(pattern, {}));
        QVERIFY(!text.contains(QString::fromUtf8(u8"到")));
        QVERIFY(text.contains(QString::fromUtf8(u8"连对")));

        pattern.type = CardPatternType::Airplane;
        pattern.mainRank = Rank::Two;
        pattern.mainLength = 2;
        text = QString::fromStdWString(CardTextFormatter::formatPlayedCards(pattern, {}));
        QVERIFY(!text.contains(QString::fromUtf8(u8"到")));
        QVERIFY(text.contains(QString::fromUtf8(u8"飞机")));
    }

private:
    static std::vector<Card> sameRank(Rank rank, int count) {
        const Suit suits[] = {Suit::Spades, Suit::Hearts, Suit::Clubs, Suit::Diamonds};
        std::vector<Card> cards;
        for (int index = 0; index < count; ++index) {
            const bool joker = rank == Rank::SmallJoker || rank == Rank::BigJoker;
            cards.push_back(Card::create(rank,
                joker ? Suit::None : suits[index % 4],
                static_cast<DeckIndex>(joker ? 0 : index / 4)));
        }
        return cards;
    }

    static std::vector<Card> sequenceOf(Rank start, int length, int copies = 1) {
        std::vector<Card> cards;
        for (int offset = 0; offset < length; ++offset) {
            const auto part = sameRank(
                static_cast<Rank>(static_cast<int>(start) + offset), copies);
            cards.insert(cards.end(), part.begin(), part.end());
        }
        return cards;
    }

    static bool containsSuit(const QString& text) {
        return text.contains(QString::fromUtf8(u8"黑桃")) ||
               text.contains(QString::fromUtf8(u8"红心")) ||
               text.contains(QString::fromUtf8(u8"梅花")) ||
               text.contains(QString::fromUtf8(u8"方块"));
    }
};

QTEST_MAIN(TestCardTextFormatter)
#include "test_card_text_formatter.moc"
