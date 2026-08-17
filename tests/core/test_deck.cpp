#include <QtTest>
#include "core/model/deck.h"
using namespace fpdz;
class TestDeck : public QObject {
    Q_OBJECT
private slots:
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
