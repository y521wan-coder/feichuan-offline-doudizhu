#pragma once
#include "../core/engine/game_state.h"
#include "../core/model/card.h"
#include <vector>
namespace fpdz {
class HintService {
public:
    static std::vector<CardId> getHint(const GameState& state, PlayerId playerId);
};
} // namespace fpdz
