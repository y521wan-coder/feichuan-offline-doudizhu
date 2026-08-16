#pragma once
#include "../engine/game_event.h"
#include "../model/game_snapshot.h"
#include <string>

namespace fpdz {
class GameTextFormatter {
public:
    static std::wstring formatEvent(const GameEvent& event);
    static std::wstring formatPlayerInfo(const PlayerPublicState& player);
    static std::wstring formatScore(int baseScore, int64_t multiplier);
    static std::wstring formatTurnContext(const PublicGameSnapshot& snap);
};
} // namespace fpdz
