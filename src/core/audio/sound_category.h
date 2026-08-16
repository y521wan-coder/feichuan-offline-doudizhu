#pragma once

#include <array>
#include <cstddef>

namespace fpdz {

enum class SoundCategory {
    StartupDeal = 0,
    BiddingLandlord,
    CardPattern,
    Pass,
    LowCards,
    Multiplier,
    YourTurn,
    CardSelection,
    InvalidAction,
    GameResult,
    BackgroundMusic,
    Count
};

constexpr std::size_t SOUND_CATEGORY_COUNT =
    static_cast<std::size_t>(SoundCategory::Count);

using SoundCategorySettings = std::array<bool, SOUND_CATEGORY_COUNT>;

constexpr std::size_t soundCategoryIndex(SoundCategory category) {
    return static_cast<std::size_t>(category);
}

} // namespace fpdz
