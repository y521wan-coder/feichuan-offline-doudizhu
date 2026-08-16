#pragma once

#include "card.h"
#include <vector>
#include <map>
#include <optional>
#include <algorithm>

namespace fpdz {

class Hand {
public:
    Hand() = default;

    void addCard(Card card);
    void addCards(const std::vector<Card>& cards);
    bool removeCard(CardId id);
    bool removeCards(const std::vector<CardId>& ids);
    bool contains(CardId id) const;
    bool hasCard(Rank rank, Suit suit, DeckIndex deckIndex) const;

    void sortByRank();

    const std::vector<Card>& cards() const { return m_cards; }
    int size() const { return static_cast<int>(m_cards.size()); }
    bool empty() const { return m_cards.empty(); }
    void clear() { m_cards.clear(); }

    // Group cards by rank
    std::map<Rank, std::vector<Card>> groupByRank() const;

    // Get count of cards with a specific rank
    int countOfRank(Rank rank) const;

    // Find cards by rank
    std::vector<Card> cardsOfRank(Rank rank) const;

private:
    std::vector<Card> m_cards;
};

} // namespace fpdz
