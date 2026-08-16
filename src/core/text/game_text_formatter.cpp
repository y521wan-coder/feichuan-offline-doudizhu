#include "game_text_formatter.h"
#include "card_text_formatter.h"
#include "../rules/card_pattern.h"
namespace fpdz {
std::wstring GameTextFormatter::formatEvent(const GameEvent& event) {
    if (event.type == GameEventType::CardsPlayed) {
        return playerIdDisplayName(event.playerId) + L"出了" +
            CardTextFormatter::formatPlayedCards(event.pattern, event.cards);
    }
    return event.message;
}
std::wstring GameTextFormatter::formatPlayerInfo(const PlayerPublicState& player) {
    std::wstring result = player.name;
    if (player.roleRevealed) result += L"(" + roleDisplayName(player.role) + L")";
    result += L"，剩余" + std::to_wstring(player.remainingCards) + L"张";
    return result;
}
std::wstring GameTextFormatter::formatScore(int baseScore, int64_t multiplier) {
    return L"基础分" + std::to_wstring(baseScore) + L"，倍数" + std::to_wstring(multiplier);
}
std::wstring GameTextFormatter::formatTurnContext(const PublicGameSnapshot& snap) {
    std::wstring result = L"轮到" + playerIdDisplayName(snap.currentPlayer) + L"出牌";
    if (snap.lastPlayedValid) {
        result += L"。上一手是" + playerIdDisplayName(snap.lastPlayedBy) + L"的牌";
    } else {
        result += L"。自由出牌";
    }
    return result;
}
} // namespace fpdz
