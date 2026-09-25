#include <QtTest>
#include <QFile>
#include <QFileInfo>

#include "core/audio/card_pattern_sound_plan.h"
#include "core/rules/pattern_analyzer.h"

using namespace fpdz;

namespace {

void appendSameRank(std::vector<Card>& cards, Rank rank, int count) {
    const Suit suits[] = {Suit::Spades, Suit::Hearts, Suit::Clubs, Suit::Diamonds};
    for (int deck = 0; deck < 2 && count > 0; ++deck) {
        for (const auto suit : suits) {
            if (count-- <= 0) break;
            cards.push_back(Card::create(rank, suit, static_cast<DeckIndex>(deck)));
        }
    }
}

GameEvent playedEvent(const std::vector<Card>& cards, PlayerId player = PlayerId::Player1,
                      int playerCount = PLAYER_COUNT) {
    GameEvent event;
    event.type = GameEventType::CardsPlayed;
    event.playerId = player;
    event.cards = cards;
    event.pattern = PatternAnalyzer::analyze(cards, playerCount);
    return event;
}

QStringList asQStringList(const std::vector<std::string>& files) {
    QStringList result;
    for (const auto& file : files) result.push_back(QString::fromStdString(file));
    return result;
}

QStringList asQStringList(const CardPatternSoundPlan& plan) {
    return asQStringList(plan.voiceFiles);
}

quint16 littleEndian16(const QByteArray& bytes, int offset) {
    return static_cast<quint16>(static_cast<quint8>(bytes[offset])) |
           (static_cast<quint16>(static_cast<quint8>(bytes[offset + 1])) << 8);
}

quint32 littleEndian32(const QByteArray& bytes, int offset) {
    return static_cast<quint32>(static_cast<quint8>(bytes[offset])) |
           (static_cast<quint32>(static_cast<quint8>(bytes[offset + 1])) << 8) |
           (static_cast<quint32>(static_cast<quint8>(bytes[offset + 2])) << 16) |
           (static_cast<quint32>(static_cast<quint8>(bytes[offset + 3])) << 24);
}

} // namespace

class TestCardPatternSoundPlan : public QObject {
    Q_OBJECT

private slots:
    void testBackgroundMusicFilesAreReadablePcm16Wave() {
        const QString soundRoot = QStringLiteral(FPDZ_SOURCE_DIR "/assets/sounds/music/");
        for (const QString& fileName : {QStringLiteral("background.wav"),
                                        QStringLiteral("normal.wav"),
                                        QStringLiteral("intense.wav")}) {
            QFile file(soundRoot + fileName);
            QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
            const QByteArray header = file.read(36);
            QCOMPARE(header.size(), 36);
            QCOMPARE(header.mid(0, 4), QByteArray("RIFF"));
            QCOMPARE(header.mid(8, 4), QByteArray("WAVE"));
            QCOMPARE(header.mid(12, 4), QByteArray("fmt "));
            QCOMPARE(littleEndian16(header, 20), quint16(1));
            QCOMPARE(littleEndian16(header, 34), quint16(16));
            const quint32 byteRate = littleEndian32(header, 28);
            QVERIFY(byteRate > 0);

            quint32 dataSize = 0;
            QVERIFY(file.seek(12));
            while (!file.atEnd()) {
                const QByteArray chunkHeader = file.read(8);
                if (chunkHeader.size() != 8) break;
                const quint32 chunkSize = littleEndian32(chunkHeader, 4);
                if (chunkHeader.mid(0, 4) == QByteArray("data")) {
                    dataSize = chunkSize;
                    break;
                }
                QVERIFY(file.seek(file.pos() + chunkSize + (chunkSize & 1U)));
            }
            QVERIFY2(dataSize > 0, qPrintable(file.fileName()));
            QVERIFY((static_cast<quint64>(dataSize) * 1000U) / byteRate > 0);
        }
    }

    void testTripleReadsCountThenRank() {
        std::vector<Card> cards;
        appendSameRank(cards, Rank::Three, 3);
        QCOMPARE(asQStringList(buildCardPatternSoundPlan(playedEvent(cards))),
                 QStringList({"card_four/boy/three.wav", "card_four/boy/3.wav"}));
    }

    void testStraightReadsOnlyRangeEndpoints() {
        std::vector<Card> cards;
        for (int value = static_cast<int>(Rank::Three);
             value <= static_cast<int>(Rank::Nine); ++value) {
            appendSameRank(cards, static_cast<Rank>(value), 1);
        }
        QCOMPARE(asQStringList(buildCardPatternSoundPlan(playedEvent(cards))),
                 QStringList({"card_four/boy/3.wav", "card_four/boy/zhi.wav",
                              "card_four/boy/9.wav", "card_four/boy/line.wav"}));
    }

    void testConsecutivePairsReadsOnlyRangeEndpoints() {
        std::vector<Card> cards;
        for (int value = static_cast<int>(Rank::Three);
             value <= static_cast<int>(Rank::Eight); ++value) {
            appendSameRank(cards, static_cast<Rank>(value), 2);
        }
        QCOMPARE(asQStringList(buildCardPatternSoundPlan(playedEvent(cards))),
                 QStringList({"card_four/boy/3.wav", "card_four/boy/zhi.wav",
                              "card_four/boy/8.wav", "card_four/boy/linkPair.wav"}));
    }

    void testAirplaneReadsOnlyRangeEndpoints() {
        std::vector<Card> cards;
        for (int value = static_cast<int>(Rank::Three);
             value <= static_cast<int>(Rank::Five); ++value) {
            appendSameRank(cards, static_cast<Rank>(value), 3);
        }
        QCOMPARE(asQStringList(buildCardPatternSoundPlan(playedEvent(cards))),
                 QStringList({"card_four/boy/3.wav", "card_four/boy/zhi.wav",
                              "card_four/boy/5.wav", "card_four/boy/linkThree.wav"}));
    }

    void testAirplaneWithPairsReadsBodyAndEveryWing() {
        std::vector<Card> cards;
        appendSameRank(cards, Rank::Queen, 3);
        appendSameRank(cards, Rank::King, 3);
        appendSameRank(cards, Rank::Three, 2);
        appendSameRank(cards, Rank::Four, 2);
        const auto event = playedEvent(cards, PlayerId::Player4);
        QCOMPARE(event.pattern.type, CardPatternType::AirplaneWithPairs);
        QCOMPARE(asQStringList(buildCardPatternSoundPlan(event)),
                 QStringList({"card_four/girl/Q.wav", "card_four/girl/zhi.wav",
                              "card_four/girl/K.wav", "card_four/girl/plane.wav",
                              "card_four/girl/pair3.wav",
                              "card_four/girl/pair4.wav"}));
    }

    void testSingleDeckAddedPatternsUseExistingVoiceFiles() {
        std::vector<Card> tripleWithSingle;
        appendSameRank(tripleWithSingle, Rank::Three, 3);
        appendSameRank(tripleWithSingle, Rank::Five, 1);
        std::vector<Card> airplaneWithSingles;
        appendSameRank(airplaneWithSingles, Rank::Three, 3);
        appendSameRank(airplaneWithSingles, Rank::Four, 3);
        appendSameRank(airplaneWithSingles, Rank::Five, 1);
        appendSameRank(airplaneWithSingles, Rank::Six, 1);
        std::vector<Card> fourWithTwoSingles;
        appendSameRank(fourWithTwoSingles, Rank::Three, 4);
        appendSameRank(fourWithTwoSingles, Rank::Five, 1);
        appendSameRank(fourWithTwoSingles, Rank::Six, 1);
        std::vector<Card> fourWithTwoPairs;
        appendSameRank(fourWithTwoPairs, Rank::Three, 4);
        appendSameRank(fourWithTwoPairs, Rank::Five, 2);
        appendSameRank(fourWithTwoPairs, Rank::Six, 2);

        const std::vector<std::pair<std::vector<Card>, CardPatternType>> cases = {
            {tripleWithSingle, CardPatternType::TripleWithSingle},
            {airplaneWithSingles, CardPatternType::AirplaneWithSingles},
            {fourWithTwoSingles, CardPatternType::FourWithTwoSingles},
            {fourWithTwoPairs, CardPatternType::FourWithTwoPairs}
        };
        const QString soundRoot = QStringLiteral(FPDZ_SOURCE_DIR "/assets/sounds/");
        for (const auto& [cards, type] : cases) {
            const auto event = playedEvent(cards, PlayerId::Player1, 3);
            QCOMPARE(event.pattern.type, type);
            for (const bool female : {false, true}) {
                const auto plan = buildCardPatternSoundPlan(event, female);
                QVERIFY(!plan.voiceFiles.empty());
                for (const auto& relativePath : plan.voiceFiles) {
                    const QString absolutePath = soundRoot + QString::fromStdString(relativePath);
                    QVERIFY2(QFileInfo::exists(absolutePath), qPrintable(absolutePath));
                }
            }
        }
        const auto event = playedEvent(airplaneWithSingles, PlayerId::Player1, 3);
        QCOMPARE(asQStringList(buildCardPatternSoundPlan(event)),
                 QStringList({"card_four/boy/3.wav", "card_four/boy/zhi.wav",
                              "card_four/boy/4.wav", "card_four/boy/plane.wav",
                              "card_four/boy/5.wav", "card_four/boy/6.wav"}));
    }

    void testFirstTwoAiUseMaleAndThirdAiUsesFemale() {
        std::vector<Card> cards;
        appendSameRank(cards, Rank::Five, 1);
        QCOMPARE(asQStringList(buildCardPatternSoundPlan(playedEvent(cards, PlayerId::Player2))),
                 QStringList({"card_four/boy/5.wav"}));
        QCOMPARE(asQStringList(buildCardPatternSoundPlan(playedEvent(cards, PlayerId::Player3))),
                 QStringList({"card_four/boy/5.wav"}));
        QCOMPARE(asQStringList(buildCardPatternSoundPlan(playedEvent(cards, PlayerId::Player4))),
                 QStringList({"card_four/girl/5.wav"}));
        QCOMPARE(asQStringList(buildCardPatternSoundPlan(playedEvent(cards), true)),
                 QStringList({"card_four/girl/5.wav"}));
    }

    void testKingBombAndDoubleKingBomb() {
        const std::vector<Card> kingBomb = {
            Card::create(Rank::SmallJoker, Suit::None, 0),
            Card::create(Rank::BigJoker, Suit::None, 0)
        };
        QCOMPARE(asQStringList(buildCardPatternSoundPlan(playedEvent(kingBomb))),
                 QStringList({"card_four/boy/shuangwangqiangbi.wav"}));

        const std::vector<Card> doubleKingBomb = {
            Card::create(Rank::SmallJoker, Suit::None, 0),
            Card::create(Rank::SmallJoker, Suit::None, 1),
            Card::create(Rank::BigJoker, Suit::None, 0),
            Card::create(Rank::BigJoker, Suit::None, 1)
        };
        const auto plan = buildCardPatternSoundPlan(playedEvent(doubleKingBomb));
        QCOMPARE(asQStringList(plan), QStringList({"card_four/boy/tianzun.wav"}));
        QCOMPARE(QString::fromStdString(plan.effectFile), QStringLiteral("card_four/tianzun.wav"));
    }

    void testEveryLegalPatternUsesExistingFiles() {
        std::vector<GameEvent> events;
        for (int count = 1; count <= 3; ++count) {
            std::vector<Card> cards;
            appendSameRank(cards, Rank::Five, count);
            events.push_back(playedEvent(cards));
        }

        std::vector<Card> tripleWithPair;
        appendSameRank(tripleWithPair, Rank::Six, 3);
        appendSameRank(tripleWithPair, Rank::Seven, 2);
        events.push_back(playedEvent(tripleWithPair));

        std::vector<Card> straight;
        for (int value = static_cast<int>(Rank::Three);
             value <= static_cast<int>(Rank::Seven); ++value) {
            appendSameRank(straight, static_cast<Rank>(value), 1);
        }
        events.push_back(playedEvent(straight));

        std::vector<Card> pairs;
        std::vector<Card> triples;
        for (int value = static_cast<int>(Rank::Three);
             value <= static_cast<int>(Rank::Five); ++value) {
            appendSameRank(pairs, static_cast<Rank>(value), 2);
            if (value <= static_cast<int>(Rank::Four)) {
                appendSameRank(triples, static_cast<Rank>(value), 3);
            }
        }
        events.push_back(playedEvent(pairs));
        events.push_back(playedEvent(triples));

        std::vector<Card> airplaneWithPairs;
        appendSameRank(airplaneWithPairs, Rank::Three, 3);
        appendSameRank(airplaneWithPairs, Rank::Four, 3);
        appendSameRank(airplaneWithPairs, Rank::Two, 2);
        appendSameRank(airplaneWithPairs, Rank::Five, 2);
        events.push_back(playedEvent(airplaneWithPairs));

        for (int count = 4; count <= 8; ++count) {
            std::vector<Card> cards;
            appendSameRank(cards, Rank::Eight, count);
            events.push_back(playedEvent(cards));
        }

        events.push_back(playedEvent({
            Card::create(Rank::SmallJoker, Suit::None, 0),
            Card::create(Rank::BigJoker, Suit::None, 0)
        }));
        events.push_back(playedEvent({
            Card::create(Rank::SmallJoker, Suit::None, 0),
            Card::create(Rank::SmallJoker, Suit::None, 1),
            Card::create(Rank::BigJoker, Suit::None, 0),
            Card::create(Rank::BigJoker, Suit::None, 1)
        }));

        const QString soundRoot = QStringLiteral(FPDZ_SOURCE_DIR "/assets/sounds/");
        for (const auto& event : events) {
            QVERIFY(event.pattern.isValid());
            const auto plan = buildCardPatternSoundPlan(event);
            QVERIFY(!plan.voiceFiles.empty() || !plan.effectFile.empty());
            for (const auto& relativePath : plan.voiceFiles) {
                const QString absolutePath = soundRoot + QString::fromStdString(relativePath);
                QVERIFY2(QFileInfo::exists(absolutePath), qPrintable(absolutePath));
            }
            if (!plan.effectFile.empty()) {
                const QString absolutePath = soundRoot + QString::fromStdString(plan.effectFile);
                QVERIFY2(QFileInfo::exists(absolutePath), qPrintable(absolutePath));
            }
        }
    }
};

QTEST_MAIN(TestCardPatternSoundPlan)
#include "test_card_pattern_sound_plan.moc"
