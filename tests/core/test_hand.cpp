#include <QtTest>
#include "core/model/hand.h"
using namespace fpdz;
class TestHand : public QObject {
    Q_OBJECT
private slots:
    void testAddAndRemove() {
        Hand hand;
        Card c = Card::create(0);
        hand.addCard(c);
        QCOMPARE(hand.size(), 1);
        QVERIFY(hand.contains(0));
        QVERIFY(hand.removeCard(0));
        QCOMPARE(hand.size(), 0);
    }
    void testRemoveNonExistent() {
        Hand hand;
        QVERIFY(!hand.removeCard(99));
    }
    void testRemoveDuplicateIdsIsAtomic() {
        Hand hand;
        hand.addCard(Card::create(0));
        QVERIFY(!hand.removeCards({0, 0}));
        QCOMPARE(hand.size(), 1);
        QVERIFY(hand.contains(0));
    }
    void testGroupByRank() {
        Hand hand;
        hand.addCard(Card::create(Rank::Five, Suit::Spades, 0));
        hand.addCard(Card::create(Rank::Five, Suit::Hearts, 0));
        hand.addCard(Card::create(Rank::Three, Suit::Clubs, 0));
        auto groups = hand.groupByRank();
        QCOMPARE(static_cast<int>(groups[Rank::Five].size()), 2);
        QCOMPARE(static_cast<int>(groups[Rank::Three].size()), 1);
    }
    void testSortByRank() {
        Hand hand;
        hand.addCard(Card::create(Rank::Ace, Suit::Spades, 0));
        hand.addCard(Card::create(Rank::Three, Suit::Hearts, 0));
        hand.addCard(Card::create(Rank::King, Suit::Clubs, 0));
        hand.sortByRank();
        QCOMPARE(hand.cards()[0].rank(), Rank::Three);
        QCOMPARE(hand.cards()[1].rank(), Rank::King);
        QCOMPARE(hand.cards()[2].rank(), Rank::Ace);
    }
};
QTEST_MAIN(TestHand)
#include "test_hand.moc"
