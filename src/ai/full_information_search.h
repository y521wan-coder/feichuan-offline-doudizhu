#pragma once

#include "ai_level_profile.h"
#include "ai_player.h"
#include "legal_move_generator.h"

#include <array>
#include <chrono>
#include <optional>
#include <vector>

namespace fpdz {

// Bounded team minimax over the single real deal copied into AiObservation.
// This class never receives or retains a GameState/GameEngine pointer.
class FullInformationSearch {
public:
    FullInformationSearch(const AiObservation& observation,
                          const AiLevelProfile& profile);

    bool active() const { return m_active; }
    std::optional<int> scoreMove(const LegalMove& move) const;
    std::optional<int> scorePass() const;

private:
    struct State {
        std::array<Hand, PLAYER_COUNT> hands;
        std::array<Role, PLAYER_COUNT> roles{};
        int activePlayerCount = PLAYER_COUNT;
        int currentPlayer = 0;
        int lastPlayedBy = 0;
        std::vector<Card> lastPlayedCards;
        int consecutivePasses = 0;
        int winner = -1;
    };

    struct Action {
        bool pass = false;
        LegalMove move;
    };

    State initialState() const;
    std::vector<Action> actions(const State& state) const;
    State apply(const State& state, const Action& action) const;
    int search(const State& state, Role perspective, int depth,
               int& nodesRemaining, int alpha, int beta) const;
    int rollout(State state, Role perspective, int plies) const;
    int heuristic(const State& state, Role perspective) const;
    int movePriority(const State& state, int actor,
                     const LegalMove& move) const;
    std::optional<int> scoreAfterRoot(const std::optional<LegalMove>& move) const;
    bool deadlineReached() const;

    const AiObservation& m_observation;
    const AiLevelProfile& m_profile;
    bool m_active = false;
    std::chrono::steady_clock::time_point m_deadline;
};

} // namespace fpdz
