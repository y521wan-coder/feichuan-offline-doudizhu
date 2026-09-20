#pragma once

#include "player.h"
#include "card.h"
#include <array>
#include <vector>
#include <cstdint>

namespace fpdz {

// Forward declarations
enum class GamePhase : uint8_t;

struct RoundResult {
    bool valid = false;
    PlayerId winner = PlayerId::Player1;
    bool landlordWon = false;
    bool spring = false;
    bool antiSpring = false;
    int64_t finalMultiplier = 1;
    std::array<int64_t, PLAYER_COUNT> scoreChanges{};
};

enum class PublicActionType : uint8_t {
    Bid,
    Play,
    Pass
};

struct PublicActionRecord {
    PublicActionType type = PublicActionType::Bid;
    PlayerId playerId = PlayerId::Player1;
    int bidValue = 0;
    std::vector<Card> cards;
    uint64_t sequence = 0;
};

// Public snapshot - only contains info the human player is allowed to know
struct PublicGameSnapshot {
    uint64_t gameId = 0;
    GamePhase phase = static_cast<GamePhase>(0);
    int activePlayerCount = PLAYER_COUNT;

    // Current player whose turn it is
    PlayerId currentPlayer = PlayerId::Player1;

    // Player public states (no hidden cards)
    std::array<PlayerPublicState, PLAYER_COUNT> players;

    // Bottom cards (hidden during bidding; public after landlord selection)
    std::vector<Card> bottomCards;
    bool bottomCardsRevealed = false;

    // Last played cards info
    std::vector<Card> lastPlayedCards;
    PlayerId lastPlayedBy = PlayerId::Player1;
    bool lastPlayedValid = false;

    // Scoring info
    int baseScore = 0;
    int64_t currentMultiplier = 1;
    int bombCount = 0;
    RoundResult roundResult;

    // Round info
    int consecutivePasses = 0;

    // Complete public history. It never contains another player's hand.
    std::vector<PublicActionRecord> actionHistory;
};

// Full game state - accessible to the engine and adjudication code only.
// AI strategies receive AiObservation and must never receive this structure.
struct FullGameState {
    uint64_t gameId = 0;
    uint64_t randomSeed = 0;
    bool deterministicRandom = false;
    int activePlayerCount = PLAYER_COUNT;

    // All player states (includes hidden hands)
    std::array<PlayerState, PLAYER_COUNT> players;

    // Bottom cards
    std::vector<Card> bottomCards;
    bool bottomCardsRevealed = false;

    // Turn management
    PlayerId currentPlayer = PlayerId::Player1;
    PlayerId lastPlayedBy = PlayerId::Player1;
    std::vector<Card> lastPlayedCards;
    int consecutivePasses = 0;

    // Bidding
    int currentBid = 0;
    PlayerId highestBidder = PlayerId::Player1;
    int highestBid = 0;
    PlayerId biddingStartPlayer = PlayerId::Player1;
    int biddingPlayerCount = 0;
    int consecutiveRedeals = 0;

    // Public bids, plays and passes, retained for fair imperfect-information AI.
    std::vector<PublicActionRecord> actionHistory;

    // Scoring
    int baseScore = 1;
    int64_t currentMultiplier = 1;
    int bombCount = 0;
    bool springDetected = false;     // 春天
    bool antiSpringDetected = false; // 反春
    RoundResult roundResult;

    // Event tracking
    uint64_t eventSequence = 0;
};

} // namespace fpdz
