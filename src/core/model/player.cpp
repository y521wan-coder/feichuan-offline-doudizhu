#include "player.h"

namespace fpdz {

std::wstring playerIdDisplayName(PlayerId id) {
    switch (id) {
        case PlayerId::Player1: return L"玩家一";
        case PlayerId::Player2: return L"玩家二";
        case PlayerId::Player3: return L"玩家三";
        case PlayerId::Player4: return L"玩家四";
    }
    return L"未知";
}

std::wstring seatDisplayName(SeatPosition seat) {
    switch (seat) {
        case SeatPosition::East:  return L"玩家一/我";
        case SeatPosition::South: return L"玩家二/下家";
        case SeatPosition::West:  return L"玩家三/对家";
        case SeatPosition::North: return L"玩家四/上家";
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

PlayerId nextPlayer(PlayerId current) {
    return static_cast<PlayerId>((static_cast<uint8_t>(current) + 1) % PLAYER_COUNT);
}

} // namespace fpdz
