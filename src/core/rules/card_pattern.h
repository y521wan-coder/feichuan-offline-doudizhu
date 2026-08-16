#pragma once

#include "../model/card.h"
#include <vector>
#include <compare>

namespace fpdz {

enum class CardPatternType {
    Invalid = 0,
    Single,           // 单张
    Pair,             // 对子
    Triple,           // 三张
    TripleWithSingle, // 三带一
    TripleWithPair,   // 三带二
    Straight,         // 顺子 (5+)
    ConsecutivePairs, // 连对 (3+ pairs)
    Airplane,         // 飞机 (不带)
    AirplaneWithSingles, // 飞机带单
    AirplaneWithPairs,   // 飞机带对
    FourWithTwoSingles,  // 四带二单
    FourWithTwoPairs,    // 四带两对
    Gun,              // 枪 (4张) - bomb x2
    KingBomb,         // 王炸 (1小王+1大王)
    Cannon,           // 炮 (5张) - bomb x4
    Rocket,           // 火箭 (6张) - bomb x8
    Missile,          // 导弹 (7张) - bomb x16
    SkyBlast,         // 天炸 (8张) - bomb x32
    HeavenlyLord      // 天尊 (2小王+2大王) - bomb x64
};

bool isBombType(CardPatternType type);
int bombMultiplier(CardPatternType type);
int bombLevel(CardPatternType type); // For comparison: higher = stronger bomb
std::wstring patternTypeName(CardPatternType type);

struct CardPattern {
    CardPatternType type = CardPatternType::Invalid;
    Rank mainRank = Rank::Three;      // Primary rank for comparison
    int mainLength = 0;               // Length of main body (for sequences/airplanes)
    int totalCards = 0;               // Total number of cards
    std::vector<Card> cards;          // The cards in this pattern (normalized order)

    bool isValid() const { return type != CardPatternType::Invalid; }
    bool isBomb() const { return isBombType(type); }

    // Create a PatternKey for comparison
    struct PatternKey {
        int bombLevel = 0;    // 0 for non-bombs
        int mainWeight = 0;   // Weight of main rank
        int mainLength = 0;   // Length of sequence/airplane
        CardPatternType type = CardPatternType::Invalid;
        int totalCards = 0;

        auto operator<=>(const PatternKey&) const = default;
    };

    PatternKey key() const;
};

} // namespace fpdz
