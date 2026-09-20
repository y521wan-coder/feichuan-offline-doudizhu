#include <QtTest>
#include "core/model/deck.h"
using namespace fpdz;
class TestDeck : public QObject {
    Q_OBJECT
private slots:
    void testCreateSingleDeckForThreePlayerMode() {
        const auto deck = Deck::createSingleDeck();
        QCOMPARE(static_cast<int>(deck.size()), THREE_PLAYER_TOTAL_CARDS);
        for (int index = 0; index < static_cast<int>(deck.size()); ++index) {
            QVERIFY(deck[static_cast<size_t>(index)].isValid());
            QCOMPARE(static_cast<int>(deck[static_cast<size_t>(index)].id()), index);
            QCOMPARE(deck[static_cast<size_t>(index)].deckIndex(), static_cast<DeckIndex>(0));
        }
        QCOMPARE(static_cast<int>(Deck::createForPlayerCount(THREE_PLAYER_COUNT).size()),
                 THREE_PLAYER_TOTAL_CARDS);
        QCOMPARE(static_cast<int>(Deck::createForPlayerCount(TWO_PLAYER_COUNT).size()),
                 TWO_PLAYER_TOTAL_CARDS);
        QCOMPARE(static_cast<int>(Deck::createForPlayerCount(PLAYER_COUNT).size()), TOTAL_CARDS);
    }
    void testCreateDoubleDeck() {
        auto deck = Deck::createDoubleDeck();
        QCOMPARE(static_cast<int>(deck.size()), 108);
    }
    void testAllCardsValid() {
        auto deck = Deck::createDoubleDeck();
        for (const auto& c : deck) QVERIFY(c.isValid());
    }
    void testShuffleChangesOrder() {
        auto deck = Deck::createDoubleDeck();
        auto original = deck;
        Deck::shuffle(deck, 42);
        bool changed = false;
        for (int i = 0; i < 108; ++i) {
            if (deck[i].id() != original[i].id()) { changed = true; break; }
        }
        QVERIFY(changed);
    }
    void testShuffleIsStableForASeed() {
        auto first = Deck::createDoubleDeck();
        auto second = Deck::createDoubleDeck();
        Deck::shuffle(first, 0x123456789ABCDEF0ULL);
        Deck::shuffle(second, 0x123456789ABCDEF0ULL);
        for (int i = 0; i < 108; ++i) QCOMPARE(first[i].id(), second[i].id());
    }
    void testDeterministicBoundedCoversSeats() {
        bool seen[4] = {false, false, false, false};
        for (uint64_t seed = 1; seed <= 100; ++seed) {
            const auto value = Deck::deterministicBounded(seed, 4);
            QVERIFY(value < 4);
            seen[value] = true;
        }
        for (bool value : seen) QVERIFY(value);
    }
};
QTEST_MAIN(TestDeck)
#include "test_deck.moc"
