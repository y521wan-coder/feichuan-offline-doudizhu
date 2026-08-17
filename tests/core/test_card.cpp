#include <QtTest>
#include "core/model/card.h"
#include "core/model/deck.h"
#include <set>
using namespace fpdz;

class TestCard : public QObject {
    Q_OBJECT
private slots:
    void testDoubleDeckHas108Cards() {
        auto deck = Deck::createDoubleDeck();
        QCOMPARE(static_cast<int>(deck.size()), TOTAL_CARDS);
    }
    void testAllCardIdsUnique() {
        auto deck = Deck::createDoubleDeck();
        std::set<CardId> ids;
        for (const auto& c : deck) ids.insert(c.id());
        QCOMPARE(static_cast<int>(ids.size()), TOTAL_CARDS);
    }
    void testEachNormalRankHas8Cards() {
        auto deck = Deck::createDoubleDeck();
        for (int r = 0; r <= static_cast<int>(Rank::Ace); ++r) {
            Rank rank = static_cast<Rank>(r);
            int count = 0;
            for (const auto& c : deck) {
                if (c.rank() == rank) count++;
            }
            QCOMPARE(count, COPIES_PER_RANK_DOUBLE);
        }
    }
    void testJokerCounts() {
        auto deck = Deck::createDoubleDeck();
        int smallJokers = 0, bigJokers = 0;
        for (const auto& c : deck) {
            if (c.rank() == Rank::SmallJoker) smallJokers++;
            if (c.rank() == Rank::BigJoker) bigJokers++;
        }
        QCOMPARE(smallJokers, 2);
        QCOMPARE(bigJokers, 2);
    }
    void testNormalCardsHaveValidSuit() {
        auto deck = Deck::createDoubleDeck();
        for (const auto& c : deck) {
            if (c.rank() != Rank::SmallJoker && c.rank() != Rank::BigJoker) {
                QVERIFY(c.suit() != Suit::None);
                QVERIFY(c.suit() <= Suit::Diamonds);
            }
        }
    }
    void testJokersHaveNoSuit() {
        auto deck = Deck::createDoubleDeck();
        for (const auto& c : deck) {
            if (c.rank() == Rank::SmallJoker || c.rank() == Rank::BigJoker) {
                QCOMPARE(c.suit(), Suit::None);
            }
        }
    }
    void testSameSeedSameOrder() {
        auto deck1 = Deck::createDoubleDeck();
        auto deck2 = Deck::createDoubleDeck();
        Deck::shuffle(deck1, 12345);
        Deck::shuffle(deck2, 12345);
        for (int i = 0; i < TOTAL_CARDS; ++i) {
            QCOMPARE(deck1[i].id(), deck2[i].id());
        }
    }
    void testDifferentSeedsDifferentOrder() {
        auto deck1 = Deck::createDoubleDeck();
        auto deck2 = Deck::createDoubleDeck();
        Deck::shuffle(deck1, 12345);
        Deck::shuffle(deck2, 67890);
        bool different = false;
        for (int i = 0; i < TOTAL_CARDS; ++i) {
            if (deck1[i].id() != deck2[i].id()) { different = true; break; }
        }
        QVERIFY(different);
    }
    void testInvalidCardCreation() {
        Card c = Card::create(Rank::SmallJoker, Suit::Spades, 0);
        QVERIFY(!c.isValid());
    }
    void testValidCardCreation() {
        Card c = Card::create(Rank::Ace, Suit::Hearts, 1);
        QVERIFY(c.isValid());
        QCOMPARE(c.rank(), Rank::Ace);
        QCOMPARE(c.suit(), Suit::Hearts);
        QCOMPARE(c.deckIndex(), static_cast<DeckIndex>(1));
    }
};
QTEST_MAIN(TestCard)
#include "test_card.moc"
