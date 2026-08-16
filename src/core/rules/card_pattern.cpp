#include "card_pattern.h"

namespace fpdz {

bool isBombType(CardPatternType type) {
    switch (type) {
        case CardPatternType::Gun:
        case CardPatternType::KingBomb:
        case CardPatternType::Cannon:
        case CardPatternType::Rocket:
        case CardPatternType::Missile:
        case CardPatternType::SkyBlast:
        case CardPatternType::HeavenlyLord:
            return true;
        default:
            return false;
    }
}

int bombMultiplier(CardPatternType type) {
    switch (type) {
        case CardPatternType::Gun:          return 2;
        case CardPatternType::KingBomb:     return 4;
        case CardPatternType::Cannon:       return 4;
        case CardPatternType::Rocket:       return 8;
        case CardPatternType::Missile:      return 16;
        case CardPatternType::SkyBlast:     return 32;
        case CardPatternType::HeavenlyLord: return 64;
        default:                            return 1;
    }
}

int bombLevel(CardPatternType type) {
    switch (type) {
        case CardPatternType::Gun:          return 1;
        case CardPatternType::KingBomb:     return 2;
        case CardPatternType::Cannon:       return 3;
        case CardPatternType::Rocket:       return 4;
        case CardPatternType::Missile:      return 5;
        case CardPatternType::SkyBlast:     return 6;
        case CardPatternType::HeavenlyLord: return 7;
        default:                            return 0;
    }
}

std::wstring patternTypeName(CardPatternType type) {
    switch (type) {
        case CardPatternType::Single:             return L"单张";
        case CardPatternType::Pair:               return L"对子";
        case CardPatternType::Triple:             return L"三张";
        case CardPatternType::TripleWithSingle:   return L"三带一";
        case CardPatternType::TripleWithPair:     return L"三带二";
        case CardPatternType::Straight:           return L"顺子";
        case CardPatternType::ConsecutivePairs:   return L"连对";
        case CardPatternType::Airplane:           return L"飞机";
        case CardPatternType::AirplaneWithSingles:return L"飞机带单";
        case CardPatternType::AirplaneWithPairs:  return L"飞机带对";
        case CardPatternType::FourWithTwoSingles: return L"四带二单";
        case CardPatternType::FourWithTwoPairs:   return L"四带两对";
        case CardPatternType::Gun:                return L"枪";
        case CardPatternType::KingBomb:           return L"王炸";
        case CardPatternType::Cannon:             return L"炮";
        case CardPatternType::Rocket:             return L"火箭";
        case CardPatternType::Missile:            return L"导弹";
        case CardPatternType::SkyBlast:           return L"天炸";
        case CardPatternType::HeavenlyLord:       return L"天尊";
        case CardPatternType::Invalid:            return L"无效";
    }
    return L"未知";
}

CardPattern::PatternKey CardPattern::key() const {
    PatternKey k;
    k.bombLevel = isBomb() ? bombLevel(type) : 0;
    k.mainWeight = rankWeight(mainRank);
    k.mainLength = mainLength;
    k.type = type;
    k.totalCards = totalCards;
    return k;
}

} // namespace fpdz
