#include "hand.h"
#include <algorithm>
#include <cassert>

namespace fpdz {

void Hand::addCard(Card card) {
    if (!card.isValid()) return;
    m_cards.push_back(card);
}

void Hand::addCards(const std::vector<Card>& cards) {
    for (const auto& card : cards) {
        addCard(card);
    }
}

bool Hand::removeCard(CardId id) {
    auto it = std::find_if(m_cards.begin(), m_cards.end(),
        [id](const Card& c) { return c.id() == id; });
    if (it == m_cards.end()) return false;
    m_cards.erase(it);
    return true;
}

bool Hand::removeCards(const std::vector<CardId>& ids) {
    // Verify all cards exist first (atomic operation)
    auto uniqueIds = ids;
    std::sort(uniqueIds.begin(), uniqueIds.end());
    if (std::adjacent_find(uniqueIds.begin(), uniqueIds.end()) != uniqueIds.end()) {
        return false;
    }
    for (CardId id : ids) {
        if (!contains(id)) return false;
    }
    for (CardId id : ids) {
        removeCard(id);
    }
    return true;
}

bool Hand::contains(CardId id) const {
    return std::any_of(m_cards.begin(), m_cards.end(),
        [id](const Card& c) { return c.id() == id; });
}

bool Hand::hasCard(Rank rank, Suit suit, DeckIndex deckIndex) const {
    return std::any_of(m_cards.begin(), m_cards.end(),
        [&](const Card& c) {
            return c.rank() == rank && c.suit() == suit && c.deckIndex() == deckIndex;
        });
}

void Hand::sortByRank() {
    std::stable_sort(m_cards.begin(), m_cards.end(),
        [](const Card& a, const Card& b) {
            if (a.weight() != b.weight()) return a.weight() < b.weight();
            if (a.suit() != b.suit()) return static_cast<int>(a.suit()) < static_cast<int>(b.suit());
            return a.deckIndex() < b.deckIndex();
        });
}

std::map<Rank, std::vector<Card>> Hand::groupByRank() const {
    std::map<Rank, std::vector<Card>> groups;
    for (const auto& card : m_cards) {
        groups[card.rank()].push_back(card);
    }
    return groups;
}

int Hand::countOfRank(Rank rank) const {
    int count = 0;
    for (const auto& card : m_cards) {
        if (card.rank() == rank) ++count;
    }
    return count;
}

std::vector<Card> Hand::cardsOfRank(Rank rank) const {
    std::vector<Card> result;
    for (const auto& card : m_cards) {
        if (card.rank() == rank) result.push_back(card);
    }
    return result;
}

} // namespace fpdz
