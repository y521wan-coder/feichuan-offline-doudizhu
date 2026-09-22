#include "ai_level_profile.h"

#include <array>

namespace fpdz {

const AiLevelProfile& aiLevelProfile(AiDifficulty level) {
    static constexpr std::array<AiLevelProfile, 3> profiles{{
        {AiDifficulty::Beginner, 150, 1, 8, 0, 2, 120, 4, 0.35},
        {AiDifficulty::Intermediate, 450, 2, 24, 24, 3, 500, 8, 0.10},
        {AiDifficulty::Advanced, 1000, 4, 64, 96, 4, 7000, 20, 0.0},
    }};
    const int index = static_cast<int>(level);
    return profiles[index >= 0 && index < static_cast<int>(profiles.size()) ? index : 1];
}

} // namespace fpdz
