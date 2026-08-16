#pragma once

#include "../engine/game_event.h"

#include <string>
#include <vector>

namespace fpdz {

struct CardPatternSoundPlan {
    std::vector<std::string> voiceFiles;
    std::string effectFile;
};

CardPatternSoundPlan buildCardPatternSoundPlan(
    const GameEvent& event, bool humanUsesFemaleVoice = false);

} // namespace fpdz
