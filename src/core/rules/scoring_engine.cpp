#include "scoring_engine.h"
#include <numeric>

namespace fpdz {

bool ScoreResult::isZeroSum() const {
    int64_t total = 0;
    for (auto s : scoreChanges) total += s;
    return total == 0;
}

ScoreResult ScoringEngine::calculate(const FullGameState& state) {
    ScoreResult result;

    // Determine winner
    // Landlord is player whose role is Landlord
    int landlordIdx = -1;
    for (int i = 0; i < state.activePlayerCount; ++i) {
        if (state.players[i].role == Role::Landlord) {
            landlordIdx = i;
            break;
        }
    }
    if (landlordIdx < 0) return result;

    // Check who ran out of cards first
    // The player with 0 remaining cards is the winner
    // (In actual game, we detect this when cards are played)
    bool landlordWon = state.players[landlordIdx].hand.empty();
    result.landlordWon = landlordWon;

    // Base score from bid
    result.baseScore = std::max(static_cast<int64_t>(state.baseScore), int64_t(1));

    // Bomb multiplier
    result.bombMultiplier = 1;
    for (int i = 0; i < state.bombCount; ++i) {
        // Each bomb doubles at minimum; actual multiplier depends on bomb type
        // For simplicity, we use the accumulated multiplier from game state
    }
    result.totalMultiplier = state.currentMultiplier;

    // Spring detection
    if (landlordWon) {
        // Spring: landlord won and no farmer played any card
        bool noFarmerPlayed = true;
        for (int i = 0; i < state.activePlayerCount; ++i) {
            if (state.players[i].role == Role::Farmer && state.players[i].hasPlayedThisRound) {
                noFarmerPlayed = false;
                break;
            }
        }
        if (noFarmerPlayed) {
            result.spring = true;
            result.totalMultiplier *= 2;
        }
    } else {
        // Anti-spring: farmers won and landlord only played once
        if (state.players[landlordIdx].cardsPlayedCount == 1) {
            result.antiSpring = true;
            result.totalMultiplier *= 2;
        }
    }

    // Apply cap
    result.totalMultiplier = applyMultiplierCap(result.totalMultiplier);

    // Calculate score changes
    int64_t baseUnit = result.baseScore * result.totalMultiplier;

    if (landlordWon) {
        result.scoreChanges[landlordIdx] = baseUnit * (state.activePlayerCount - 1);
        for (int i = 0; i < state.activePlayerCount; ++i) {
            if (i != landlordIdx) {
                result.scoreChanges[i] = -baseUnit;
            }
        }
    } else {
        result.scoreChanges[landlordIdx] = -baseUnit * (state.activePlayerCount - 1);
        for (int i = 0; i < state.activePlayerCount; ++i) {
            if (i != landlordIdx) {
                result.scoreChanges[i] = baseUnit;
            }
        }
    }

    return result;
}

int64_t ScoringEngine::applyMultiplierCap(int64_t multiplier, int64_t cap) {
    if (multiplier > cap) return cap;
    return multiplier;
}

} // namespace fpdz
