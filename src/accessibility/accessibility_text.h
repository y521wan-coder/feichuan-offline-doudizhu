#pragma once
#include "../core/model/game_snapshot.h"
#include <string>
namespace fpdz {
class AccessibilityText {
public:
    static std::wstring formatHandCard(const Card& card, int position, int total, bool selected, int sameRankCount);
    static std::wstring formatPlayerStatus(const PlayerPublicState& player);
    static std::wstring formatGameContext(const PublicGameSnapshot& snap);
};
} // namespace fpdz
