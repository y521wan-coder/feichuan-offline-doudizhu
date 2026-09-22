#include "full_information_search.h"

#include "../core/rules/pattern_analyzer.h"

#include <algorithm>
#include <array>
#include <limits>

namespace fpdz {
namespace {

std::vector<CardId> idsOf(const LegalMove& move) {
    std::vector<CardId> ids;
    ids.reserve(move.cards.size());
    for (const auto& card : move.cards) ids.push_back(card.id());
    return ids;
}

bool sameTeam(Role left, Role right) {
    return left != Role::Undetermined && left == right;
}

bool highControl(const LegalMove& move) {
    return move.pattern.isBomb() ||
           rankWeight(move.pattern.mainRank) >= rankWeight(Rank::Two);
}

int quickTurns(const Hand& hand) {
    if (hand.empty()) return 0;
    std::array<int, RANK_COUNT> counts{};
    for (const auto& card : hand.cards()) ++counts[rankWeight(card.rank())];

    int turns = 0;
    for (const auto [copies, minimumLength] :
         {std::pair{3, 2}, std::pair{2, 3}, std::pair{1, 5}}) {
        for (;;) {
            int bestStart = -1;
            int bestLength = 0;
            int start = rankWeight(Rank::Three);
            while (start <= rankWeight(Rank::Ace)) {
                while (start <= rankWeight(Rank::Ace) && counts[start] < copies) ++start;
                int end = start;
                while (end <= rankWeight(Rank::Ace) && counts[end] >= copies) ++end;
                if (end - start > bestLength) {
                    bestStart = start;
                    bestLength = end - start;
                }
                start = std::max(end, start + 1);
            }
            if (bestLength < minimumLength) break;
            for (int rank = bestStart; rank < bestStart + bestLength; ++rank) {
                counts[rank] -= copies;
            }
            ++turns;
        }
    }
    int triples = 0;
    int pairs = 0;
    for (const int count : counts) {
        if (count <= 0) continue;
        ++turns;
        if (count >= 3) ++triples;
        else if (count >= 2) ++pairs;
    }
    return std::max(1, turns - std::min(triples, pairs));
}

int lowestRemaining(const std::array<Hand, PLAYER_COUNT>& hands,
                    const std::array<Role, PLAYER_COUNT>& roles,
                    int activePlayerCount, Role role, bool turns) {
    int result = 99;
    for (int player = 0; player < activePlayerCount; ++player) {
        if (roles[player] != role) continue;
        result = std::min(result, turns ? quickTurns(hands[player]) : hands[player].size());
    }
    return result;
}

int teamControl(const std::array<Hand, PLAYER_COUNT>& hands,
                const std::array<Role, PLAYER_COUNT>& roles,
                int activePlayerCount, Role role) {
    int score = 0;
    for (int player = 0; player < activePlayerCount; ++player) {
        if (roles[player] != role) continue;
        std::array<int, RANK_COUNT> counts{};
        for (const auto& card : hands[player].cards()) {
            ++counts[rankWeight(card.rank())];
            if (card.rank() >= Rank::Two) score += 18;
        }
        for (const int count : counts) {
            if (count >= 4) score += 70 + (count - 4) * 25;
        }
    }
    return score;
}

template <typename StateType>
int totalCards(const StateType& state) {
    int total = 0;
    for (int player = 0; player < state.activePlayerCount; ++player) {
        total += state.hands[player].size();
    }
    return total;
}

} // namespace

FullInformationSearch::FullInformationSearch(
    const AiObservation& observation, const AiLevelProfile& profile)
    : m_observation(observation), m_profile(profile),
      m_deadline(std::chrono::steady_clock::now() +
                 std::chrono::milliseconds(profile.decisionBudgetMs)) {
    m_active = observation.phase == GamePhase::Playing &&
        observation.fullInformation.available &&
        isSupportedPlayerCount(observation.publicState.activePlayerCount);
}

bool FullInformationSearch::deadlineReached() const {
    return std::chrono::steady_clock::now() >= m_deadline;
}

FullInformationSearch::State FullInformationSearch::initialState() const {
    State state;
    state.hands = m_observation.fullInformation.allHands;
    state.activePlayerCount = m_observation.publicState.activePlayerCount;
    state.currentPlayer = static_cast<int>(m_observation.playerId);
    state.lastPlayedBy = static_cast<int>(m_observation.publicState.lastPlayedBy);
    state.lastPlayedCards = m_observation.publicState.lastPlayedCards;
    state.consecutivePasses = m_observation.publicState.consecutivePasses;
    for (int player = 0; player < state.activePlayerCount; ++player) {
        state.roles[player] = m_observation.publicState.players[player].role;
    }
    return state;
}

int FullInformationSearch::movePriority(const State& state, int actor,
                                        const LegalMove& move) const {
    Hand remaining = state.hands[actor];
    remaining.removeCards(idsOf(move));
    if (remaining.empty()) return 100000;
    const bool leader = state.lastPlayedCards.empty();
    int score = -quickTurns(remaining) * 520 - remaining.size() * 24;
    score += static_cast<int>(move.cards.size()) * (leader ? 90 : 55);
    score -= rankWeight(move.pattern.mainRank) * (leader ? 3 : 9);
    if (move.pattern.isBomb()) score -= 650;

    int closestEnemy = 99;
    int closestTeammate = 99;
    for (int player = 0; player < state.activePlayerCount; ++player) {
        if (player == actor) continue;
        if (sameTeam(state.roles[actor], state.roles[player])) {
            closestTeammate = std::min(closestTeammate, state.hands[player].size());
        } else {
            closestEnemy = std::min(closestEnemy, state.hands[player].size());
        }
    }
    if (closestEnemy <= 2) score += static_cast<int>(move.cards.size()) * 230;
    if (leader && state.roles[actor] == Role::Farmer && closestTeammate == 1 &&
        move.pattern.type == CardPatternType::Single && !highControl(move)) {
        score += 900 - rankWeight(move.pattern.mainRank) * 25;
    }
    return score;
}

std::vector<FullInformationSearch::Action>
FullInformationSearch::actions(const State& state) const {
    const int actor = state.currentPlayer;
    const bool leader = state.lastPlayedCards.empty();
    std::optional<CardPattern> lastPattern;
    if (!leader) {
        lastPattern = PatternAnalyzer::analyze(
            state.lastPlayedCards, state.activePlayerCount);
    }
    auto moves = LegalMoveGenerator::generateLegalMoves(
        state.hands[actor], lastPattern, state.activePlayerCount);
    std::vector<Action> result;
    auto appendMoves = [&](const auto& predicate) {
        for (const auto& move : moves) {
            if (predicate(move)) result.push_back(Action{false, move});
        }
    };

    appendMoves([&](const LegalMove& move) {
        return static_cast<int>(move.cards.size()) == state.hands[actor].size();
    });
    if (!result.empty()) return result;

    if (!leader) {
        const Role lastRole = state.roles[state.lastPlayedBy];
        if (state.roles[actor] == Role::Farmer && lastRole == Role::Farmer) {
            int landlordCards = 99;
            for (int player = 0; player < state.activePlayerCount; ++player) {
                if (state.roles[player] == Role::Landlord) {
                    landlordCards = state.hands[player].size();
                    break;
                }
            }
            if (landlordCards > 1) return {Action{true, {}}};
            appendMoves([](const LegalMove& move) {
                return !move.pattern.isBomb() && !highControl(move);
            });
            if (result.empty()) appendMoves([](const LegalMove& move) {
                return !move.pattern.isBomb();
            });
            if (result.empty()) appendMoves([](const LegalMove&) { return true; });
        } else {
            appendMoves([](const LegalMove&) { return true; });
            result.push_back(Action{true, {}});
        }
    } else {
        appendMoves([](const LegalMove&) { return true; });
    }
    if (result.empty()) return {Action{true, {}}};

    std::stable_sort(result.begin(), result.end(), [&](const Action& left,
                                                       const Action& right) {
        if (left.pass != right.pass) return !left.pass;
        if (left.pass) return false;
        return movePriority(state, actor, left.move) >
               movePriority(state, actor, right.move);
    });
    const int moveLimit = m_profile.level == AiDifficulty::Advanced ? 7
        : (m_profile.level == AiDifficulty::Intermediate ? 4 : 2);
    const auto pass = std::find_if(result.begin(), result.end(),
        [](const Action& action) { return action.pass; });
    const bool keepPass = pass != result.end();
    Action passAction;
    if (keepPass) passAction = *pass;
    std::erase_if(result, [](const Action& action) { return action.pass; });
    if (static_cast<int>(result.size()) > moveLimit) result.resize(moveLimit);
    if (keepPass) result.push_back(std::move(passAction));
    return result;
}

FullInformationSearch::State
FullInformationSearch::apply(const State& state, const Action& action) const {
    State next = state;
    const int actor = state.currentPlayer;
    if (!action.pass) {
        next.hands[actor].removeCards(idsOf(action.move));
        if (next.hands[actor].empty()) {
            next.winner = actor;
            return next;
        }
        next.lastPlayedCards = action.move.cards;
        next.lastPlayedBy = actor;
        next.consecutivePasses = 0;
        next.currentPlayer = (actor + 1) % next.activePlayerCount;
        return next;
    }
    ++next.consecutivePasses;
    if (!next.lastPlayedCards.empty() &&
        next.consecutivePasses >= next.activePlayerCount - 1) {
        next.currentPlayer = next.lastPlayedBy;
        next.lastPlayedCards.clear();
        next.consecutivePasses = 0;
    } else {
        next.currentPlayer = (actor + 1) % next.activePlayerCount;
    }
    return next;
}

int FullInformationSearch::heuristic(const State& state, Role perspective) const {
    if (state.winner >= 0) {
        return sameTeam(state.roles[state.winner], perspective) ? 12000 : -12000;
    }
    const Role opponent = perspective == Role::Landlord ? Role::Farmer : Role::Landlord;
    const int ownTurns = lowestRemaining(
        state.hands, state.roles, state.activePlayerCount, perspective, true);
    const int enemyTurns = lowestRemaining(
        state.hands, state.roles, state.activePlayerCount, opponent, true);
    const int ownCards = lowestRemaining(
        state.hands, state.roles, state.activePlayerCount, perspective, false);
    const int enemyCards = lowestRemaining(
        state.hands, state.roles, state.activePlayerCount, opponent, false);
    int result = (enemyTurns - ownTurns) * 620 + (enemyCards - ownCards) * 55;
    result += teamControl(state.hands, state.roles, state.activePlayerCount, perspective);
    result -= teamControl(state.hands, state.roles, state.activePlayerCount, opponent);
    result += sameTeam(state.roles[state.currentPlayer], perspective) ? 85 : -85;
    if (!state.lastPlayedCards.empty()) {
        result += sameTeam(state.roles[state.lastPlayedBy], perspective) ? 70 : -70;
    }
    if (perspective == Role::Farmer) {
        const int farmerCards = ownCards;
        const int landlordCards = enemyCards;
        if (farmerCards <= 2) result += (3 - farmerCards) * 240;
        if (landlordCards <= 2) result -= (3 - landlordCards) * 320;
    }
    return result;
}

int FullInformationSearch::search(const State& state, Role perspective,
                                  int depth, int& nodesRemaining,
                                  int alpha, int beta) const {
    if (state.winner >= 0 || depth <= 0 || nodesRemaining <= 0 ||
        deadlineReached()) {
        return heuristic(state, perspective);
    }
    const auto candidates = actions(state);
    if (candidates.empty()) return heuristic(state, perspective);
    const bool maximize = sameTeam(state.roles[state.currentPlayer], perspective);
    int best = maximize ? std::numeric_limits<int>::min()
                        : std::numeric_limits<int>::max();
    for (const auto& action : candidates) {
        if (nodesRemaining-- <= 0 || deadlineReached()) break;
        const int score = search(apply(state, action), perspective, depth - 1,
                                 nodesRemaining, alpha, beta);
        if (maximize) {
            best = std::max(best, score);
            alpha = std::max(alpha, best);
        } else {
            best = std::min(best, score);
            beta = std::min(beta, best);
        }
        if (beta <= alpha) break;
    }
    return best == std::numeric_limits<int>::min() ||
           best == std::numeric_limits<int>::max()
        ? heuristic(state, perspective) : best;
}

int FullInformationSearch::rollout(State state, Role perspective,
                                   int plies) const {
    for (int ply = 0; ply < plies && state.winner < 0 && !deadlineReached(); ++ply) {
        const auto candidates = actions(state);
        if (candidates.empty()) break;
        const Role actorRole = state.roles[state.currentPlayer];
        int bestScore = std::numeric_limits<int>::min();
        const Action* best = &candidates.front();
        for (const auto& action : candidates) {
            const State next = apply(state, action);
            int score = heuristic(next, actorRole);
            if (!action.pass) {
                score += movePriority(state, state.currentPlayer, action.move) / 8;
            }
            if (score > bestScore) {
                bestScore = score;
                best = &action;
            }
        }
        state = apply(state, *best);
    }
    return heuristic(state, perspective);
}

std::optional<int> FullInformationSearch::scoreAfterRoot(
    const std::optional<LegalMove>& move) const {
    if (!m_active || deadlineReached()) return std::nullopt;
    State state = initialState();
    state = apply(state, move.has_value() ? Action{false, *move}
                                          : Action{true, {}});
    const Role perspective = state.roles[static_cast<int>(m_observation.playerId)];
    if (state.winner >= 0) return heuristic(state, perspective);

    int nodes = m_profile.fullInformationNodeBudget;
    const int remaining = totalCards(state);
    const int endgameLimit = state.activePlayerCount * 6 + 2;
    if (remaining <= endgameLimit) {
        return search(state, perspective, m_profile.searchDepth + 3, nodes,
                      std::numeric_limits<int>::min() / 2,
                      std::numeric_limits<int>::max() / 2);
    }
    const int minimaxScore = search(state, perspective, m_profile.searchDepth,
                                    nodes,
                                    std::numeric_limits<int>::min() / 2,
                                    std::numeric_limits<int>::max() / 2);
    const int rolloutScore = rollout(std::move(state), perspective,
                                     m_profile.rolloutPlies);
    return (minimaxScore * 2 + rolloutScore) / 3;
}

std::optional<int> FullInformationSearch::scoreMove(const LegalMove& move) const {
    return scoreAfterRoot(move);
}

std::optional<int> FullInformationSearch::scorePass() const {
    if (m_observation.publicState.lastPlayedCards.empty()) return std::nullopt;
    return scoreAfterRoot(std::nullopt);
}

} // namespace fpdz
