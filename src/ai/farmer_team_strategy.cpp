#include "farmer_team_strategy.h"

#include <algorithm>

namespace fpdz {
namespace {

bool finishesHand(const LegalMove& move, const Hand& hand) {
    return static_cast<int>(move.cards.size()) == hand.size();
}

bool isHighControlMove(const LegalMove& move) {
    return move.pattern.isBomb() || rankWeight(move.pattern.mainRank) >= rankWeight(Rank::Two);
}

int landlordIndex(const PublicGameSnapshot& state) {
    for (int index = 0; index < PLAYER_COUNT; ++index) {
        if (state.players[index].role == Role::Landlord) return index;
    }
    return -1;
}

std::vector<std::size_t> indexesMatching(const std::vector<LegalMove>& moves,
                                         const auto& predicate) {
    std::vector<std::size_t> result;
    for (std::size_t index = 0; index < moves.size(); ++index) {
        if (predicate(moves[index])) result.push_back(index);
    }
    return result;
}

const PlayerPublicState* closestFinishingTeammate(const AiObservation& observation) {
    const PlayerPublicState* best = nullptr;
    for (const auto& player : observation.publicState.players) {
        if (player.id == observation.playerId || player.role != Role::Farmer) continue;
        if (!best || player.remainingCards < best->remainingCards) best = &player;
    }
    return best;
}

CardPatternType inferredUsefulLeadType(const AiObservation& observation,
                                       PlayerId teammate) {
    for (auto it = observation.publicState.actionHistory.rbegin();
         it != observation.publicState.actionHistory.rend(); ++it) {
        if (it->playerId != teammate || it->type != PublicActionType::Play) continue;
        if (it->cards.size() == 2 && it->cards[0].rank() == it->cards[1].rank()) {
            return CardPatternType::Pair;
        }
        return CardPatternType::Single;
    }
    return CardPatternType::Single;
}

} // namespace

FarmerTeamDecision FarmerTeamStrategy::selectCandidates(
    const AiObservation& observation, const std::vector<LegalMove>& moves, bool isLeader) {
    FarmerTeamDecision decision;
    if (moves.empty()) {
        decision.pass = true;
        decision.reasonCode = "no_legal_move";
        return decision;
    }

    // Priority 1: any farmer finishing immediately ends the game for the team.
    decision.allowedMoveIndexes = indexesMatching(moves, [&](const LegalMove& move) {
        return finishesHand(move, observation.ownHand);
    });
    if (!decision.allowedMoveIndexes.empty()) {
        decision.reasonCode = "farmer_immediate_team_win";
        if (!isLeader) {
            const auto lastRole = observation.publicState.players[
                static_cast<int>(observation.publicState.lastPlayedBy)].role;
            decision.teamRuleException = lastRole == Role::Farmer;
        }
        return decision;
    }

    const int landlord = landlordIndex(observation.publicState);
    const int landlordCards = landlord >= 0
        ? observation.publicState.players[landlord].remainingCards : 99;
    const bool landlordImmediateThreat = landlordCards <= 1;
    const Role lastRole = isLeader ? Role::Undetermined
        : observation.publicState.players[
              static_cast<int>(observation.publicState.lastPlayedBy)].role;

    // Priorities 2-4, 7 and 8: normally yield to a teammate. The only runtime
    // exception available without hidden information is a visible one-card
    // landlord threat; the selected response is recorded as an exception.
    if (!isLeader && lastRole == Role::Farmer) {
        if (!landlordImmediateThreat) {
            decision.pass = true;
            decision.reasonCode = "yield_to_farmer_teammate";
            decision.allowedMoveIndexes.clear();
            return decision;
        }
        decision.allowedMoveIndexes = indexesMatching(moves, [](const LegalMove& move) {
            return !move.pattern.isBomb() && !isHighControlMove(move);
        });
        if (decision.allowedMoveIndexes.empty()) {
            decision.allowedMoveIndexes = indexesMatching(moves, [](const LegalMove& move) {
                return !move.pattern.isBomb();
            });
        }
        if (decision.allowedMoveIndexes.empty()) {
            decision.allowedMoveIndexes = indexesMatching(moves, [](const LegalMove&) {
                return true;
            });
        }
        decision.reasonCode = "exception_landlord_one_card_threat";
        decision.teamRuleException = true;
        return decision;
    }

    // Priorities 5-6: lead a low single, or a publicly inferred single/pair,
    // when a teammate is close to going out.
    if (isLeader) {
        if (const auto* teammate = closestFinishingTeammate(observation)) {
            if (teammate->remainingCards == 1) {
                decision.allowedMoveIndexes = indexesMatching(moves, [](const LegalMove& move) {
                    return move.pattern.type == CardPatternType::Single &&
                           rankWeight(move.pattern.mainRank) < rankWeight(Rank::Two);
                });
                if (!decision.allowedMoveIndexes.empty()) {
                    decision.reasonCode = "feed_low_single_to_one_card_teammate";
                    return decision;
                }
            } else if (teammate->remainingCards == 2) {
                const auto preferred = inferredUsefulLeadType(observation, teammate->id);
                decision.allowedMoveIndexes = indexesMatching(moves, [&](const LegalMove& move) {
                    return move.pattern.type == preferred && !isHighControlMove(move);
                });
                if (!decision.allowedMoveIndexes.empty()) {
                    decision.reasonCode = preferred == CardPatternType::Pair
                        ? "feed_pair_to_two_card_teammate"
                        : "feed_single_to_two_card_teammate";
                    return decision;
                }
            }
        }
    }

    decision.allowedMoveIndexes = indexesMatching(moves, [](const LegalMove&) { return true; });

    // Priorities 9-10: when responding to the landlord, keep bombs and high
    // controls out of the candidate set whenever a cheaper response exists.
    if (!isLeader && lastRole == Role::Landlord) {
        auto economical = indexesMatching(moves, [](const LegalMove& move) {
            return !move.pattern.isBomb() && !isHighControlMove(move);
        });
        if (economical.empty()) {
            economical = indexesMatching(moves, [](const LegalMove& move) {
                return !move.pattern.isBomb();
            });
        }
        if (!economical.empty()) decision.allowedMoveIndexes = std::move(economical);
        decision.reasonCode = landlordImmediateThreat
            ? "block_landlord_immediate_threat"
            : "minimum_cost_response_to_landlord";
        return decision;
    }

    decision.reasonCode = "farmer_free_play_team_shape";
    return decision;
}

int FarmerTeamStrategy::scoreAdjustment(const AiObservation& observation,
                                        const LegalMove& move, bool isLeader,
                                        Role lastRole) {
    int adjustment = 0;
    int landlordCards = 99;
    if (const int landlord = landlordIndex(observation.publicState); landlord >= 0) {
        landlordCards = observation.publicState.players[landlord].remainingCards;
    }
    if (!isLeader && lastRole == Role::Farmer && landlordCards <= 1) {
        // The visible one-card threat is a hard team-rule exception. Use the
        // lowest adequate interception even when private hand planning would
        // prefer spending a higher card.
        adjustment -= rankWeight(move.pattern.mainRank) * 1000;
        if (move.pattern.isBomb()) adjustment -= 50000;
    }
    if (!isLeader && lastRole == Role::Landlord) {
        // This dominates private hand-shape optimization: the farmer uses the
        // lowest adequate public response and retains control resources.
        adjustment -= rankWeight(move.pattern.mainRank) * 1000;
        adjustment -= static_cast<int>(move.cards.size()) * 20;
        if (move.pattern.isBomb()) adjustment -= 50000;
    }

    if (landlordCards <= 2) {
        adjustment += static_cast<int>(move.cards.size()) * 250;
    }
    return adjustment;
}

} // namespace fpdz
