#pragma once

#include <string>

namespace fpdz {

enum class GamePhase : uint8_t {
    NotStarted = 0,
    Dealing,
    Bidding,
    RevealingBottomCards,
    Playing,
    Paused,
    Settling,
    Finished
};

std::wstring gamePhaseName(GamePhase phase);

} // namespace fpdz
