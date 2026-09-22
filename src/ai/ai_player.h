#pragma once

#include "ai_difficulty.h"
#include "../core/engine/game_command.h"
#include "../core/engine/game_state.h"
#include "../core/model/deck.h"

#include <array>

namespace fpdz {

struct AiFullInformation {
    bool available = false;
    std::array<Hand, PLAYER_COUNT> allHands;
    std::vector<Card> hiddenBottomCards;
    std::vector<Card> setAsideCards;
};

struct AiObservation {
    PlayerId playerId = PlayerId::Player1;
    GamePhase phase = GamePhase::NotStarted;
    Hand ownHand;
    PublicGameSnapshot publicState;
    AiFullInformation fullInformation;
    int highestBid = 0;
    uint64_t decisionSeed = 0;
};

inline AiObservation makeAiObservation(const GameState& state, PlayerId playerId) {
    AiObservation observation;
    observation.playerId = playerId;
    observation.phase = state.phase();
    observation.ownHand = state.fullState().players[static_cast<int>(playerId)].hand;
    observation.publicState = state.publicSnapshot();
    const auto& full = state.fullState();
    const int activePlayerCount = full.activePlayerCount;
    std::array<bool, TOTAL_CARDS> occupied{};
    bool valid = isSupportedPlayerCount(activePlayerCount);
    for (int index = 0; index < PLAYER_COUNT; ++index) {
        if (index < activePlayerCount) {
            observation.fullInformation.allHands[index] = full.players[index].hand;
            for (const auto& card : full.players[index].hand.cards()) {
                if (!card.isValid() || card.id() >= totalCardsForPlayerCount(activePlayerCount) ||
                    occupied[card.id()]) {
                    valid = false;
                } else {
                    occupied[card.id()] = true;
                }
            }
        }
    }
    observation.fullInformation.hiddenBottomCards = full.bottomCards;
    for (const auto& action : full.actionHistory) {
        if (action.type != PublicActionType::Play) continue;
        for (const auto& card : action.cards) {
            if (!card.isValid() || card.id() >= totalCardsForPlayerCount(activePlayerCount) ||
                occupied[card.id()]) {
                valid = false;
            } else {
                occupied[card.id()] = true;
            }
        }
    }
    for (const auto& card : full.bottomCards) {
        if (!card.isValid() || card.id() >= totalCardsForPlayerCount(activePlayerCount)) {
            valid = false;
        }
        // After bidding, bottom cards are also present in the landlord's hand
        // (or public play history), so they are intentionally not treated as
        // duplicate ownership here.
        occupied[card.id()] = true;
    }
    if (activePlayerCount == TWO_PLAYER_COUNT) {
        for (const auto& card : Deck::createForPlayerCount(activePlayerCount)) {
            if (!occupied[card.id()]) {
                observation.fullInformation.setAsideCards.push_back(card);
            }
        }
        valid = valid &&
            observation.fullInformation.setAsideCards.size() == TWO_PLAYER_SET_ASIDE_CARDS;
    } else {
        for (int id = 0; id < totalCardsForPlayerCount(activePlayerCount); ++id) {
            if (!occupied[static_cast<std::size_t>(id)]) valid = false;
        }
    }
    valid = valid && static_cast<int>(full.bottomCards.size()) ==
        bottomCardsForPlayerCount(activePlayerCount);
    observation.fullInformation.available = valid;
    observation.highestBid = state.fullState().highestBid;
    observation.decisionSeed = state.gameId() * 1000003ULL +
        static_cast<uint64_t>(static_cast<int>(playerId) + 1) * 97ULL +
        state.fullState().eventSequence;
    return observation;
}

class AiPlayer {
public:
    virtual ~AiPlayer() = default;
    virtual AiDifficulty difficulty() const = 0;
    virtual GameCommand decideBid(const AiObservation& observation) = 0;
    virtual GameCommand decidePlay(const AiObservation& observation) = 0;

    GameCommand decideBid(const GameState& state, PlayerId playerId) {
        return decideBid(makeAiObservation(state, playerId));
    }
    GameCommand decidePlay(const GameState& state, PlayerId playerId) {
        return decidePlay(makeAiObservation(state, playerId));
    }
};

} // namespace fpdz
