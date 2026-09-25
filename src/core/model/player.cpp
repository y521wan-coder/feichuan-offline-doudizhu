#include "player.h"

namespace fpdz {

// 玩家称呼统一用数字 1～4，避免“玩家一、玩家二……”啰嗦；
// 用户自定义名称由 AppSettings::playerNames 覆盖。
std::wstring playerIdDisplayName(PlayerId id) {
    switch (id) {
        case PlayerId::Player1: return L"1";
        case PlayerId::Player2: return L"2";
        case PlayerId::Player3: return L"3";
        case PlayerId::Player4: return L"4";
    }
    return L"未知";
}

std::wstring seatDisplayName(SeatPosition seat) {
    switch (seat) {
        case SeatPosition::East:  return L"1/我";
        case SeatPosition::South: return L"2/下家";
        case SeatPosition::West:  return L"3/对家";
        case SeatPosition::North: return L"4/上家";
    }
    return L"未知";
}

std::wstring roleDisplayName(Role role) {
    switch (role) {
        case Role::Undetermined: return L"未定";
        case Role::Landlord:     return L"地主";
        case Role::Farmer:       return L"农民";
    }
    return L"未知";
}

PlayerId nextPlayer(PlayerId current, int activePlayerCount) {
    const int count = isSupportedPlayerCount(activePlayerCount)
        ? activePlayerCount : PLAYER_COUNT;
    return static_cast<PlayerId>((static_cast<uint8_t>(current) + 1) % count);
}

} // namespace fpdz
