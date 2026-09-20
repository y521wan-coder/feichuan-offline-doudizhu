#pragma once

#include "../core/model/hand.h"
#include "../core/rules/card_pattern.h"
#include <optional>
#include <vector>

namespace fpdz {

struct LegalMove {
    std::vector<Card> cards;
    CardPattern pattern;
};

class LegalMoveGenerator {
public:
    static std::vector<LegalMove> generateLegalMoves(
        const Hand& hand, const std::optional<CardPattern>& lastPlay = std::nullopt,
        int activePlayerCount = PLAYER_COUNT);
    static std::vector<std::vector<Card>> generateFreePlayMoves(
        const Hand& hand, int activePlayerCount = PLAYER_COUNT);
    static std::vector<std::vector<Card>> generateResponseMoves(const Hand& hand,
        const CardPattern& lastPlay, int activePlayerCount = PLAYER_COUNT);
};

} // namespace fpdz
