#include "card.h"
#include <stdexcept>
#include <cassert>

namespace fpdz {

int rankWeight(Rank rank) {
    return static_cast<int>(rank);
}

bool canBeInSequence(Rank rank) {
    // 2 and jokers cannot be in sequences
    return rank >= Rank::Three && rank <= Rank::Ace;
}

std::wstring rankDisplayName(Rank rank) {
    switch (rank) {
        case Rank::Three:      return L"3";
        case Rank::Four:       return L"4";
        case Rank::Five:       return L"5";
        case Rank::Six:        return L"6";
        case Rank::Seven:      return L"7";
        case Rank::Eight:      return L"8";
        case Rank::Nine:       return L"9";
        case Rank::Ten:        return L"10";
        case Rank::Jack:       return L"J";
        case Rank::Queen:      return L"Q";
        case Rank::King:       return L"K";
        case Rank::Ace:        return L"A";
        case Rank::Two:        return L"2";
        case Rank::SmallJoker: return L"小王";
        case Rank::BigJoker:   return L"大王";
    }
    return L"?";
}

std::wstring suitDisplayName(Suit suit) {
    switch (suit) {
        case Suit::Spades:   return L"黑桃";
        case Suit::Hearts:   return L"红心";
        case Suit::Clubs:    return L"梅花";
        case Suit::Diamonds: return L"方块";
        case Suit::None:     return L"";
    }
    return L"";
}

Card Card::create(CardId id) {
    if (id >= TOTAL_CARDS) {
        Card c;
        c.m_valid = false;
        return c;
    }

    // Layout: cards 0-53 are deck 0, cards 54-107 are deck 1
    // Within each deck:
    //   0-3:   Three (Spades, Hearts, Clubs, Diamonds)
    //   4-7:   Four
    //   ...
    //   48-51: Ace
    //   52:    SmallJoker
    //   53:    BigJoker

    DeckIndex deckIndex = static_cast<DeckIndex>(id / CARDS_PER_DECK);
    int withinDeck = id % CARDS_PER_DECK;

    Rank rank;
    Suit suit;

    if (withinDeck < 52) {
        // Normal cards: 13 ranks * 4 suits
        int rankIndex = withinDeck / 4;
        int suitIndex = withinDeck % 4;
        rank = static_cast<Rank>(rankIndex);
        suit = static_cast<Suit>(suitIndex);
    } else if (withinDeck == 52) {
        rank = Rank::SmallJoker;
        suit = Suit::None;
    } else { // withinDeck == 53
        rank = Rank::BigJoker;
        suit = Suit::None;
    }

    Card c;
    c.m_id = id;
    c.m_rank = rank;
    c.m_suit = suit;
    c.m_deckIndex = deckIndex;
    c.m_valid = true;
    return c;
}

Card Card::create(Rank rank, Suit suit, DeckIndex deckIndex) {
    // Validate combination
    if (rank == Rank::SmallJoker || rank == Rank::BigJoker) {
        if (suit != Suit::None) {
            Card c;
            c.m_valid = false;
            return c;
        }
    } else {
        if (suit == Suit::None || suit > Suit::Diamonds) {
            Card c;
            c.m_valid = false;
            return c;
        }
    }

    if (deckIndex > 1) {
        Card c;
        c.m_valid = false;
        return c;
    }

    CardId id;
    if (rank == Rank::SmallJoker) {
        id = static_cast<CardId>(deckIndex * CARDS_PER_DECK + 52);
    } else if (rank == Rank::BigJoker) {
        id = static_cast<CardId>(deckIndex * CARDS_PER_DECK + 53);
    } else {
        int rankIdx = static_cast<int>(rank);
        int suitIdx = static_cast<int>(suit);
        id = static_cast<CardId>(deckIndex * CARDS_PER_DECK + rankIdx * 4 + suitIdx);
    }

    Card c;
    c.m_id = id;
    c.m_rank = rank;
    c.m_suit = suit;
    c.m_deckIndex = deckIndex;
    c.m_valid = true;
    return c;
}

std::wstring Card::displayName() const {
    if (!m_valid) return L"无效牌";
    std::wstring result;
    if (m_suit != Suit::None) {
        result = suitDisplayName(m_suit) + rankDisplayName(m_rank);
    } else {
        result = rankDisplayName(m_rank);
    }
    return result;
}

} // namespace fpdz
