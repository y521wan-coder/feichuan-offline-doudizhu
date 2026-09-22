#pragma once

#include "ai_difficulty.h"

namespace fpdz {

struct AiLevelProfile {
    AiDifficulty level;
    int decisionBudgetMs;
    int searchDepth;
    int candidateLimit;
    int publicInferenceSamples;
    int fullInformationRootCandidates;
    int fullInformationNodeBudget;
    int rolloutPlies;
    double safeChoiceRandomness;
};

const AiLevelProfile& aiLevelProfile(AiDifficulty level);

} // namespace fpdz
