#pragma once

#include "../model/player.h"
#include "../model/game_snapshot.h"
#include "rule_set.h"
#include <array>
#include <cstdint>

namespace fpdz {

struct ScoreResult {
    bool landlordWon = false;
    int64_t baseScore = 1;
    int64_t totalMultiplier = 1;
    int bombMultiplier = 1;
    bool spring = false;
    bool antiSpring = false;
    std::array<int64_t, PLAYER_COUNT> scoreChanges{};

    bool isZeroSum() const;
};

class ScoringEngine {
public:
    static ScoreResult calculate(const FullGameState& state);

    // Apply multiplier cap
    static int64_t applyMultiplierCap(int64_t multiplier, int64_t cap = RuleSet::MAX_MULTIPLIER);
};

} // namespace fpdz
