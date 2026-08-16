#include "hint_service.h"
#include "legal_move_generator.h"
#include "../core/rules/pattern_analyzer.h"
#include "../core/rules/pattern_comparator.h"
namespace fpdz {
std::vector<CardId> HintService::getHint(const GameState& state, PlayerId playerId) {
    const auto& fs = state.fullState();
    const auto& player = fs.players[static_cast<int>(playerId)];
    bool isLeader = fs.lastPlayedCards.empty();
    if (isLeader) {
        auto moves = LegalMoveGenerator::generateFreePlayMoves(player.hand);
        if (!moves.empty()) {
            std::vector<CardId> ids;
            for (const auto& c : moves[0]) ids.push_back(c.id());
            return ids;
        }
    } else {
        CardPattern lastPattern = PatternAnalyzer::analyze(fs.lastPlayedCards);
        auto moves = LegalMoveGenerator::generateResponseMoves(player.hand, lastPattern);
        for (const auto& move : moves) {
            CardPattern p = PatternAnalyzer::analyze(move);
            if (p.isValid() && PatternComparator::canBeat(p, lastPattern)) {
                std::vector<CardId> ids;
                for (const auto& c : move) ids.push_back(c.id());
                return ids;
            }
        }
    }
    return {};
}
} // namespace fpdz
