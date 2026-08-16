#include "accessibility_text.h"
#include "../core/text/card_text_formatter.h"
namespace fpdz {
std::wstring AccessibilityText::formatHandCard(const Card& card, int position, int total, bool selected, int sameRankCount) {
    return CardTextFormatter::formatCardWithSelection(card, selected, position, total, sameRankCount);
}
std::wstring AccessibilityText::formatPlayerStatus(const PlayerPublicState& player) {
    std::wstring result = player.name;
    if (player.roleRevealed) result += L"(" + roleDisplayName(player.role) + L")";
    result += L"，剩余" + std::to_wstring(player.remainingCards) + L"张";
    return result;
}
std::wstring AccessibilityText::formatGameContext(const PublicGameSnapshot& snap) {
    std::wstring result = L"当前轮到" + playerIdDisplayName(snap.currentPlayer);
    result += L"，基础分" + std::to_wstring(snap.baseScore);
    result += L"，倍数" + std::to_wstring(snap.currentMultiplier);
    return result;
}
} // namespace fpdz
