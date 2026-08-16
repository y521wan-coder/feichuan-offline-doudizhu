#pragma once

#include "../model/player.h"
#include "../model/card.h"
#include "game_event.h"
#include <vector>
#include <string>
#include <cstdint>
#include <optional>

namespace fpdz {

enum class GameCommandType : uint8_t {
    StartGame,
    Bid,
    PlayCards,
    Pass,
    Pause,
    Resume,
    AbandonGame,
    StartNextRound,
    RestoreGame
};

struct GameCommand {
    GameCommandType type = GameCommandType::StartGame;
    PlayerId playerId = PlayerId::Player1;
    int bidValue = 0;
    std::vector<CardId> cardIds;
    // No value means a real game using operating-system entropy. Tests and
    // training pass a seed to obtain a stable, reproducible deal.
    std::optional<uint64_t> randomSeed;
    bool allowPassAsLeader = false;
};

enum class ErrorCode : uint16_t {
    Success = 0,
    InvalidPhase,
    NotCurrentPlayer,
    InvalidBid,
    BidTooLow,
    InvalidCards,
    InvalidPattern,
    CannotBeatLastPlay,
    CannotPassAsLeader,
    CardsNotInHand,
    DuplicateCardId,
    GameAlreadyStarted,
    NoGameToResume,
    SaveFailed,
    RestoreFailed,
    InternalError
};

struct CommandResult {
    bool success = false;
    ErrorCode errorCode = ErrorCode::Success;
    std::wstring userMessage;
    std::vector<GameEvent> events;
    bool stateChanged = false;
};

} // namespace fpdz
