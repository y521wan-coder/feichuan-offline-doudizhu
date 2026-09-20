#include "imperfect_information_search.h"

#include "../core/model/deck.h"
#include "../core/rules/pattern_analyzer.h"

#include <algorithm>
#include <array>
#include <limits>
#include <numeric>
#include <random>

namespace fpdz {
namespace {

constexpr std::uint64_t kSearchSamplingSalt = 0x4953465F53454152ULL;

std::vector<CardId> idsOf(const LegalMove& move) {
    std::vector<CardId> result;
    result.reserve(move.cards.size());
    for (const auto& card : move.cards) result.push_back(card.id());
    return result;
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
        while (true) {
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

int totalCards(const std::array<Hand, PLAYER_COUNT>& hands, int activePlayerCount) {
    int result = 0;
    for (int player = 0; player < activePlayerCount; ++player) {
        result += hands[player].size();
    }
    return result;
}

} // namespace

ImperfectInformationSearch::ImperfectInformationSearch(
    const AiObservation& observation, const AiLevelProfile& profile)
    : m_observation(observation), m_profile(profile) {
    if (observation.phase != GamePhase::Playing ||
        profile.publicInferenceSamples <= 0 ||
        profile.level == AiDifficulty::Beginner) {
        return;
    }

    const int own = static_cast<int>(observation.playerId);
    const Role ownRole = observation.publicState.players[own].role;
    int hostileCards = 99;
    int publicTotal = observation.ownHand.size();
    for (int player = 0; player < observation.publicState.activePlayerCount; ++player) {
        if (player == own) continue;
        const auto& publicPlayer = observation.publicState.players[player];
        publicTotal += publicPlayer.remainingCards;
        if (!sameTeam(ownRole, publicPlayer.role)) {
            hostileCards = std::min(hostileCards, publicPlayer.remainingCards);
        }
    }

    const bool advancedWindow = profile.level == AiDifficulty::Advanced &&
        (observation.ownHand.size() <= 16 || hostileCards <= 10 ||
         publicTotal <= observation.publicState.activePlayerCount * 11);
    const bool intermediateWindow = profile.level == AiDifficulty::Intermediate &&
        (observation.ownHand.size() <= 8 || hostileCards <= 4);
    if (!advancedWindow && !intermediateWindow) return;
    m_samples = buildSamples();
}

std::vector<ImperfectInformationSearch::Sample>
ImperfectInformationSearch::buildSamples() const {
    const int activePlayerCount = m_observation.publicState.activePlayerCount;
    const int own = static_cast<int>(m_observation.playerId);
    const int requested = m_profile.level == AiDifficulty::Advanced ? 8 : 3;
    const int sampleCount = std::min(requested, m_profile.publicInferenceSamples);

    std::array<bool, TOTAL_CARDS> unavailable{};
    for (const auto& card : m_observation.ownHand.cards()) unavailable[card.id()] = true;
    for (const auto& record : m_observation.publicState.actionHistory) {
        if (record.type != PublicActionType::Play) continue;
        for (const auto& card : record.cards) unavailable[card.id()] = true;
    }

    int landlord = -1;
    std::array<int, PLAYER_COUNT> capacities{};
    for (int player = 0; player < activePlayerCount; ++player) {
        if (m_observation.publicState.players[player].role == Role::Landlord) {
            landlord = player;
        }
        if (player != own) {
            capacities[player] = std::max(
                0, m_observation.publicState.players[player].remainingCards);
        }
    }

    Sample fixed;
    if (m_observation.publicState.bottomCardsRevealed && landlord >= 0 &&
        landlord != own) {
        for (const auto& card : m_observation.publicState.bottomCards) {
            if (unavailable[card.id()] || capacities[landlord] <= 0) continue;
            fixed.hands[landlord].addCard(card);
            unavailable[card.id()] = true;
            --capacities[landlord];
        }
    }

    std::vector<Card> unknownCards;
    const int deckSize = totalCardsForPlayerCount(activePlayerCount);
    for (int id = 0; id < deckSize; ++id) {
        if (!unavailable[id]) unknownCards.push_back(Card::create(static_cast<CardId>(id)));
    }
    const int required = std::accumulate(capacities.begin(), capacities.end(), 0);
    if (required <= 0 || static_cast<int>(unknownCards.size()) < required) return {};

    std::vector<Sample> result;
    result.reserve(sampleCount);
    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
        Sample sample = fixed;
        auto remaining = capacities;
        auto cards = unknownCards;
        std::mt19937_64 random(m_observation.decisionSeed ^ kSearchSamplingSalt ^
            (static_cast<std::uint64_t>(sampleIndex + 1) * 0x9E3779B97F4A7C15ULL));
        std::shuffle(cards.begin(), cards.end(), random);

        int assigned = 0;
        for (const auto& card : cards) {
            if (assigned >= required) break;
            std::array<int, PLAYER_COUNT> weights{};
            int totalWeight = 0;
            const bool highCard = card.rank() >= Rank::Ace;
            for (int player = 0; player < activePlayerCount; ++player) {
                if (remaining[player] <= 0) continue;
                const auto& publicPlayer = m_observation.publicState.players[player];
                int weight = remaining[player] * 100;
                if (highCard) {
                    weight += remaining[player] * std::max(0, publicPlayer.bidScore) * 18;
                    if (publicPlayer.hasPassedBid) weight = weight * 9 / 10;
                }
                weights[player] = std::max(1, weight);
                totalWeight += weights[player];
            }
            if (totalWeight <= 0) break;
            std::uniform_int_distribution<int> choose(1, totalWeight);
            int ticket = choose(random);
            int selected = 0;
            for (; selected < activePlayerCount; ++selected) {
                ticket -= weights[selected];
                if (ticket <= 0) break;
            }
            selected = std::min(selected, activePlayerCount - 1);
            sample.hands[selected].addCard(card);
            --remaining[selected];
            ++assigned;
        }
        if (assigned == required) {
            for (int player = 0; player < activePlayerCount; ++player) {
                sample.hands[player].sortByRank();
            }
            result.push_back(std::move(sample));
        }
    }
    return result;
}

ImperfectInformationSearch::State
ImperfectInformationSearch::initialState(const Sample& sample) const {
    State state;
    state.hands = sample.hands;
    state.activePlayerCount = m_observation.publicState.activePlayerCount;
    state.currentPlayer = static_cast<int>(m_observation.playerId);
    state.lastPlayedBy = static_cast<int>(m_observation.publicState.lastPlayedBy);
    state.lastPlayedCards = m_observation.publicState.lastPlayedCards;
    state.consecutivePasses = m_observation.publicState.consecutivePasses;
    for (int player = 0; player < state.activePlayerCount; ++player) {
        state.roles[player] = m_observation.publicState.players[player].role;
    }
    state.hands[state.currentPlayer] = m_observation.ownHand;
    return state;
}

int ImperfectInformationSearch::movePriority(const State& state, int actor,
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

std::vector<ImperfectInformationSearch::Action>
ImperfectInformationSearch::actions(const State& state) const {
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
            if (result.empty()) {
                appendMoves([](const LegalMove& move) { return !move.pattern.isBomb(); });
            }
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

    const int moveLimit = m_profile.level == AiDifficulty::Advanced ? 7 : 4;
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

ImperfectInformationSearch::State
ImperfectInformationSearch::apply(const State& state, const Action& action) const {
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

int ImperfectInformationSearch::heuristic(const State& state, Role perspective) const {
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
    if (sameTeam(state.roles[state.currentPlayer], perspective)) result += 85;
    else result -= 85;
    if (!state.lastPlayedCards.empty()) {
        result += sameTeam(state.roles[state.lastPlayedBy], perspective) ? 70 : -70;
    }
    return result;
}

int ImperfectInformationSearch::search(const State& state, Role perspective,
                                       int depth, int& nodesRemaining,
                                       int alpha, int beta) const {
    if (state.winner >= 0 || depth <= 0 || nodesRemaining <= 0) {
        return heuristic(state, perspective);
    }
    const auto candidates = actions(state);
    if (candidates.empty()) return heuristic(state, perspective);
    const bool maximize = sameTeam(state.roles[state.currentPlayer], perspective);
    int best = maximize ? std::numeric_limits<int>::min()
                        : std::numeric_limits<int>::max();
    for (const auto& action : candidates) {
        if (nodesRemaining-- <= 0) break;
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
    return best;
}

int ImperfectInformationSearch::rollout(State state, Role perspective,
                                        int plies) const {
    for (int ply = 0; ply < plies && state.winner < 0; ++ply) {
        const auto candidates = actions(state);
        if (candidates.empty()) break;
        const Role actorRole = state.roles[state.currentPlayer];
        int bestScore = std::numeric_limits<int>::min();
        const Action* best = &candidates.front();
        for (const auto& action : candidates) {
            const State next = apply(state, action);
            int score = heuristic(next, actorRole);
            if (!action.pass) score += movePriority(state, state.currentPlayer, action.move) / 8;
            if (score > bestScore) {
                bestScore = score;
                best = &action;
            }
        }
        state = apply(state, *best);
    }
    return heuristic(state, perspective);
}

int ImperfectInformationSearch::scoreStatesAfterRoot(
    const std::optional<LegalMove>& move) const {
    if (m_samples.empty()) return 0;
    const int own = static_cast<int>(m_observation.playerId);
    const Role perspective = m_observation.publicState.players[own].role;
    int64_t sum = 0;
    int worst = std::numeric_limits<int>::max();
    int evaluated = 0;
    for (const auto& sample : m_samples) {
        State state = initialState(sample);
        state = apply(state, move.has_value() ? Action{false, *move}
                                              : Action{true, {}});
        int score = 0;
        const int remaining = totalCards(state.hands, state.activePlayerCount);
        const int endgameLimit = state.activePlayerCount * 5 + 2;
        if (remaining <= endgameLimit) {
            int nodes = m_profile.level == AiDifficulty::Advanced ? 650 : 180;
            const int depth = m_profile.level == AiDifficulty::Advanced
                ? std::min(9, m_profile.searchDepth + 4)
                : std::min(5, m_profile.searchDepth + 2);
            score = search(state, perspective, depth, nodes,
                           std::numeric_limits<int>::min() / 2,
                           std::numeric_limits<int>::max() / 2);
        } else {
            const int plies = m_profile.level == AiDifficulty::Advanced ? 12 : 6;
            score = rollout(std::move(state), perspective, plies);
        }
        sum += score;
        worst = std::min(worst, score);
        ++evaluated;
    }
    if (evaluated == 0) return 0;
    const int average = static_cast<int>(sum / evaluated);
    return (average * 3 + worst) / 4;
}

std::optional<int> ImperfectInformationSearch::scoreMove(
    const LegalMove& move) const {
    if (!active()) return std::nullopt;
    return scoreStatesAfterRoot(move);
}

std::optional<int> ImperfectInformationSearch::scorePass() const {
    if (!active() || m_observation.publicState.lastPlayedCards.empty()) {
        return std::nullopt;
    }
    return scoreStatesAfterRoot(std::nullopt);
}

} // namespace fpdz
