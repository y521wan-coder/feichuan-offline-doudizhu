#pragma once

#include "hand.h"
#include <string>
#include <optional>

namespace fpdz {

enum class PlayerId : uint8_t {
    Player1 = 0,  // Human player
    Player2 = 1,  // AI - next seat
    Player3 = 2,  // AI - across
    Player4 = 3   // AI - previous seat
};

enum class SeatPosition : uint8_t {
    East = 0,   // Player1 (human)
    South = 1,  // Player2 (下家)
    West = 2,   // Player3 (对家)
    North = 3   // Player4 (上家)
};

enum class Role : uint8_t {
    Undetermined = 0,
    Landlord = 1,     // 地主
    Farmer = 2        // 农民
};

struct PlayerState {
    PlayerId id = PlayerId::Player1;
    std::wstring name;
    SeatPosition seat = SeatPosition::East;
    Role role = Role::Undetermined;
    Hand hand;
    int bidScore = 0;          // 叫分 (0 = not bid yet or passed)
    bool hasPassedBid = false; // 是否已放弃叫分
    bool isHuman = false;
    int cardsPlayedCount = 0;  // Number of times this player has played (for spring detection)
    bool hasPlayedThisRound = false; // Whether the player has played at least once
    bool lastActionWasPass = false;

    int remainingCards() const { return hand.size(); }
};

// Public-facing player info (no hidden cards)
struct PlayerPublicState {
    PlayerId id = PlayerId::Player1;
    std::wstring name;
    SeatPosition seat = SeatPosition::East;
    Role role = Role::Undetermined;
    bool roleRevealed = false;
    int remainingCards = 0;
    int bidScore = 0;
    bool hasPassedBid = false;
    bool lastActionWasPass = false;
};

std::wstring playerIdDisplayName(PlayerId id);
std::wstring seatDisplayName(SeatPosition seat);
std::wstring roleDisplayName(Role role);

// Get next player in clockwise order
PlayerId nextPlayer(PlayerId current, int activePlayerCount = PLAYER_COUNT);

} // namespace fpdz

