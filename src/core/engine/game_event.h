#pragma once

#include "../model/player.h"
#include "../model/card.h"
#include "../rules/card_pattern.h"
#include <cstdint>
#include <array>
#include <string>
#include <vector>
#include <variant>

namespace fpdz {

enum class GameEventType : uint8_t {
    GameStarted,
    CardsDealt,
    BidRequested,
    PlayerBid,
    LandlordDetermined,
    BottomCardsRevealed,
    TurnChanged,
    CardsPlayed,
    PlayerPassed,
    TrickReset,
    MultiplierChanged,
    PlayerLowCards,
    InvalidAction,
    GamePaused,
    GameResumed,
    GameFinished,
    SaveCompleted,
    BackendStatusChanged
};

struct GameEvent {
    uint64_t eventId = 0;
    uint64_t gameId = 0;
    uint64_t sequence = 0;
    GameEventType type = GameEventType::GameStarted;

    // Payload
    PlayerId playerId = PlayerId::Player1;
    int bidValue = 0;
    std::vector<Card> cards;
    CardPattern pattern;
    int64_t multiplier = 1;
    int remainingCards = 0;
    bool landlordWon = false;
    bool spring = false;
    bool antiSpring = false;
    std::array<int64_t, PLAYER_COUNT> scoreChanges{};
    bool isPublic = true; // Whether this event can be shown to the human player
    std::wstring message;
};

} // namespace fpdz
