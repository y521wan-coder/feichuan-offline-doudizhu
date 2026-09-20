#pragma once

#include "ai_level_profile.h"
#include "ai_player.h"
#include "legal_move_generator.h"

#include <array>
#include <optional>
#include <vector>

namespace fpdz {

// Bounded perfect-information searches over deterministic deals sampled only
// from AiObservation.  The real hidden deal is never supplied to this class.
class ImperfectInformationSearch {
public:
    ImperfectInformationSearch(const AiObservation& observation,
                               const AiLevelProfile& profile);

    bool active() const { return !m_samples.empty(); }
    std::optional<int> scoreMove(const LegalMove& move) const;
    std::optional<int> scorePass() const;

private:
    struct Sample {
        std::array<Hand, PLAYER_COUNT> hands;
    };

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

    std::vector<Sample> buildSamples() const;
    State initialState(const Sample& sample) const;
    std::vector<Action> actions(const State& state) const;
    State apply(const State& state, const Action& action) const;
    int search(const State& state, Role perspective, int depth,
               int& nodesRemaining, int alpha, int beta) const;
    int rollout(State state, Role perspective, int plies) const;
    int heuristic(const State& state, Role perspective) const;
    int movePriority(const State& state, int actor,
                     const LegalMove& move) const;
    int scoreStatesAfterRoot(const std::optional<LegalMove>& move) const;

    const AiObservation& m_observation;
    const AiLevelProfile& m_profile;
    std::vector<Sample> m_samples;
};

} // namespace fpdz
