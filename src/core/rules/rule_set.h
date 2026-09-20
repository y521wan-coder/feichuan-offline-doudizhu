#pragma once

#include <string>
#include <cstdint>

namespace fpdz {

struct RuleSet {
    static constexpr int VERSION = 1;
    static constexpr int MAX_MULTIPLIER = 4096;
    static constexpr int BOTTOM_CARD_COUNT = 8;
    static constexpr int CARDS_PER_PLAYER = 25;
    static constexpr int MAX_BID = 3;
    static constexpr int MIN_STRAIGHT_LENGTH = 5;
    static constexpr int MIN_CONSECUTIVE_PAIRS = 3;
    static constexpr int MIN_AIRPLANE_LENGTH = 2;

    int ruleVersion = VERSION;
    std::wstring ruleSetName = L"二人/三人/四人斗地主V1.0";

    static RuleSet defaultRules();
};

} // namespace fpdz
