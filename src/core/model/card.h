#pragma once

#include <cstdint>
#include <compare>
#include <string>
#include <array>

namespace fpdz {

// Strong types for type safety
enum class Suit : uint8_t {
    Spades = 0,   // 黑桃
    Hearts,       // 红心
    Clubs,        // 梅花
    Diamonds,     // 方块
    None          // 王
};

enum class Rank : uint8_t {
    Three = 0,
    Four,
    Five,
    Six,
    Seven,
    Eight,
    Nine,
    Ten,
    Jack,
    Queen,
    King,
    Ace,
    Two,
    SmallJoker,  // 小王
    BigJoker     // 大王
};

using CardId = uint8_t;      // 0-107
using DeckIndex = uint8_t;   // 0 or 1 (which copy of the deck)

// Rank weight for comparison: higher weight = stronger card
int rankWeight(Rank rank);

// Whether a rank can participate in sequences (straight, consecutive pairs, airplane)
bool canBeInSequence(Rank rank);

// Display name for a rank
std::wstring rankDisplayName(Rank rank);
std::wstring suitDisplayName(Suit suit);

class Card {
public:
    Card() = default;

    // Factory: creates a card with validation
    // Returns a Card; isValid() will be false if combination is invalid
    static Card create(CardId id);
    static Card create(Rank rank, Suit suit, DeckIndex deckIndex);

    CardId id() const { return m_id; }
    Rank rank() const { return m_rank; }
    Suit suit() const { return m_suit; }
    DeckIndex deckIndex() const { return m_deckIndex; }
    bool isValid() const { return m_valid; }

    int weight() const { return rankWeight(m_rank); }

    auto operator<=>(const Card&) const = default;

    // Display name in Chinese
    std::wstring displayName() const;

private:
    CardId m_id = 0;
    Rank m_rank = Rank::Three;
    Suit m_suit = Suit::Spades;
    DeckIndex m_deckIndex = 0;
    bool m_valid = false;
};

// Maximum sizes used by the four-player variant and fixed-size public arrays.
constexpr int TOTAL_CARDS = 108;
constexpr int CARDS_PER_DECK = 54;
constexpr int BOTTOM_CARDS = 8;
constexpr int CARDS_PER_PLAYER = 25;
constexpr int LANDLORD_TOTAL = 33; // 25 + 8
constexpr int PLAYER_COUNT = 4;

// Supported local variants. PLAYER_COUNT remains the fixed storage capacity so
// existing four-player saves and APIs stay compatible; activePlayerCount in a
// game state selects how many entries participate in the current round.
constexpr int TWO_PLAYER_COUNT = 2;
constexpr int TWO_PLAYER_TOTAL_CARDS = 54;
constexpr int TWO_PLAYER_BOTTOM_CARDS = 3;
constexpr int TWO_PLAYER_CARDS_PER_PLAYER = 17;
constexpr int TWO_PLAYER_LANDLORD_TOTAL = 20;
constexpr int TWO_PLAYER_SET_ASIDE_CARDS = 17;
constexpr int THREE_PLAYER_COUNT = 3;
constexpr int THREE_PLAYER_TOTAL_CARDS = 54;
constexpr int THREE_PLAYER_BOTTOM_CARDS = 3;
constexpr int THREE_PLAYER_CARDS_PER_PLAYER = 17;
constexpr int THREE_PLAYER_LANDLORD_TOTAL = 20;

constexpr bool isSupportedPlayerCount(int playerCount) {
    return playerCount == TWO_PLAYER_COUNT ||
           playerCount == THREE_PLAYER_COUNT ||
           playerCount == PLAYER_COUNT;
}

constexpr int totalCardsForPlayerCount(int playerCount) {
    return playerCount == TWO_PLAYER_COUNT || playerCount == THREE_PLAYER_COUNT
        ? THREE_PLAYER_TOTAL_CARDS : TOTAL_CARDS;
}

constexpr int bottomCardsForPlayerCount(int playerCount) {
    return playerCount == TWO_PLAYER_COUNT || playerCount == THREE_PLAYER_COUNT
        ? THREE_PLAYER_BOTTOM_CARDS : BOTTOM_CARDS;
}

constexpr int cardsPerPlayerForPlayerCount(int playerCount) {
    return playerCount == TWO_PLAYER_COUNT || playerCount == THREE_PLAYER_COUNT
        ? THREE_PLAYER_CARDS_PER_PLAYER : CARDS_PER_PLAYER;
}

constexpr int landlordCardsForPlayerCount(int playerCount) {
    return cardsPerPlayerForPlayerCount(playerCount) + bottomCardsForPlayerCount(playerCount);
}

// Number of distinct ranks
constexpr int RANK_COUNT = 15; // 3-A, 2, SmallJoker, BigJoker

// Copies per rank in double deck
constexpr int COPIES_PER_RANK_DOUBLE = 8; // 4 suits * 2 decks
constexpr int JOKER_COUNT_DOUBLE = 4;     // 2 small + 2 big

} // namespace fpdz
