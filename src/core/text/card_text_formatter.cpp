#include "card_text_formatter.h"

#include <algorithm>
#include <array>
#include <utility>

namespace fpdz {

namespace {

std::wstring formatBombRankSpeech(Rank rank) {
    switch (rank) {
    case Rank::Three: return L"三";
    case Rank::Four:  return L"四";
    case Rank::Five:  return L"五";
    case Rank::Six:   return L"六";
    case Rank::Seven: return L"七";
    case Rank::Eight: return L"八";
    case Rank::Nine:  return L"九";
    case Rank::Ten:   return L"十";
    case Rank::Two:   return L"二";
    default:          return CardTextFormatter::formatRankSpeech(rank);
    }
}

std::wstring formatBombCount(int count) {
    switch (count) {
    case 4: return L"四";
    case 5: return L"五";
    case 6: return L"六";
    case 7: return L"七";
    case 8: return L"八";
    default: return std::to_wstring(count);
    }
}

int bombCardCount(const CardPattern& pattern, const std::vector<Card>& cards) {
    if (!cards.empty()) return static_cast<int>(cards.size());
    if (pattern.totalCards > 0) return pattern.totalCards;
    switch (pattern.type) {
    case CardPatternType::Gun:      return 4;
    case CardPatternType::Cannon:   return 5;
    case CardPatternType::Rocket:   return 6;
    case CardPatternType::Missile:  return 7;
    case CardPatternType::SkyBlast: return 8;
    default:                        return 0;
    }
}

std::wstring formatBomb(const CardPattern& pattern,
                        const std::vector<Card>& cards,
                        const wchar_t* name) {
    return formatBombCount(bombCardCount(pattern, cards)) + L"个" +
           formatBombRankSpeech(pattern.mainRank) + L"，" + name;
}

std::wstring formatSequence(const CardPattern& pattern, const wchar_t* name) {
    std::wstring text;
    const int start = rankWeight(pattern.mainRank);
    for (int offset = 0; offset < pattern.mainLength; ++offset) {
        if (!text.empty()) text += L"、";
        text += CardTextFormatter::formatRankSpeech(static_cast<Rank>(start + offset));
    }
    if (!text.empty()) text += L"，";
    text += name;
    return text;
}

std::wstring formatSequenceEndpoints(const CardPattern& pattern, const wchar_t* name) {
    int minimumLength = 0;
    switch (pattern.type) {
    case CardPatternType::Straight:         minimumLength = 5; break;
    case CardPatternType::ConsecutivePairs: minimumLength = 3; break;
    case CardPatternType::Airplane:         minimumLength = 2; break;
    default:                                return formatSequence(pattern, name);
    }
    const int start = rankWeight(pattern.mainRank);
    if (pattern.mainLength < minimumLength || pattern.mainLength > 12) {
        return formatSequence(pattern, name);
    }
    if (start < static_cast<int>(Rank::Three) || start > static_cast<int>(Rank::Ace)) {
        return formatSequence(pattern, name);
    }
    const int end = start + pattern.mainLength - 1;
    if (end > static_cast<int>(Rank::Ace)) {
        return formatSequence(pattern, name);
    }
    return CardTextFormatter::formatRankSpeech(pattern.mainRank) + L"到" +
           CardTextFormatter::formatRankSpeech(static_cast<Rank>(end)) + name;
}

std::wstring formatPairWings(const CardPattern& pattern,
                             const std::vector<Card>& cards,
                             int bodyCopies) {
    std::array<int, RANK_COUNT> counts{};
    for (const auto& card : cards) {
        if (card.isValid()) ++counts[static_cast<size_t>(card.rank())];
    }
    const int start = rankWeight(pattern.mainRank);
    for (int offset = 0; offset < pattern.mainLength; ++offset) {
        counts[static_cast<size_t>(start + offset)] -= bodyCopies;
    }

    const std::array<Rank, RANK_COUNT> speechOrder = {
        Rank::Two, Rank::Three, Rank::Four, Rank::Five, Rank::Six,
        Rank::Seven, Rank::Eight, Rank::Nine, Rank::Ten, Rank::Jack,
        Rank::Queen, Rank::King, Rank::Ace, Rank::SmallJoker, Rank::BigJoker
    };
    std::wstring text;
    for (const auto rank : speechOrder) {
        if (counts[static_cast<size_t>(rank)] == 2) {
            if (!text.empty()) text += L"、";
            text += L"对" + CardTextFormatter::formatRankSpeech(rank);
        }
    }
    return text;
}

} // namespace

std::wstring CardTextFormatter::formatRankSpeech(Rank rank) {
    switch (rank) {
    case Rank::Three:      return L"3";
    case Rank::Four:       return L"4";
    case Rank::Five:       return L"5";
    case Rank::Six:        return L"6";
    case Rank::Seven:      return L"7";
    case Rank::Eight:      return L"8";
    case Rank::Nine:       return L"9";
    case Rank::Ten:        return L"10";
    case Rank::Jack:       return L"钩";
    case Rank::Queen:      return L"圈";
    case Rank::King:       return L"k";
    case Rank::Ace:        return L"尖";
    case Rank::Two:        return L"2";
    case Rank::SmallJoker: return L"小王";
    case Rank::BigJoker:   return L"大王";
    }
    return L"?";
}

std::wstring CardTextFormatter::formatSameRankSpeech(Rank rank, int count) {
    if (count <= 0) count = 1;
    if (count == 2) return L"对" + formatRankSpeech(rank);
    return std::to_wstring(count) + L"张" + formatRankSpeech(rank);
}

std::wstring CardTextFormatter::formatPlayedCards(
    const CardPattern& pattern, const std::vector<Card>& cards,
    int activePlayerCount) {
    switch (pattern.type) {
    case CardPatternType::Triple:
        return L"三个" + formatRankSpeech(pattern.mainRank);
    case CardPatternType::TripleWithPair: {
        const auto wings = formatPairWings(pattern, cards, 3);
        return L"三个" + formatRankSpeech(pattern.mainRank) +
               (wings.empty() ? std::wstring{} : L"，带" + wings);
    }
    case CardPatternType::Straight:
        return formatSequenceEndpoints(pattern, L"顺子");
    case CardPatternType::ConsecutivePairs:
        return formatSequenceEndpoints(pattern, L"连对");
    case CardPatternType::Airplane:
        return formatSequenceEndpoints(pattern, L"飞机");
    case CardPatternType::AirplaneWithPairs: {
        const auto wings = formatPairWings(pattern, cards, 3);
        return formatSequence(pattern, L"飞机") +
               (wings.empty() ? std::wstring{} : L"，带" + wings);
    }
    case CardPatternType::Gun:          return formatBomb(pattern, cards, L"枪");
    case CardPatternType::KingBomb:     return activePlayerCount == 4 ? L"双王枪毙" : L"王炸";
    case CardPatternType::Cannon:       return formatBomb(pattern, cards, L"炮");
    case CardPatternType::Rocket:       return formatBomb(pattern, cards, L"火箭");
    case CardPatternType::Missile:      return formatBomb(pattern, cards, L"导弹");
    case CardPatternType::SkyBlast:     return formatBomb(pattern, cards, L"天炸");
    case CardPatternType::HeavenlyLord: return activePlayerCount == 4 ? L"天尊无敌" : L"天尊";
    default:                            return formatCards(cards);
    }
}

std::wstring CardTextFormatter::formatCard(const Card& card) {
    if (!card.isValid()) return L"无效牌";
    return formatSameRankSpeech(card.rank(), 1);
}

std::wstring CardTextFormatter::formatCards(const std::vector<Card>& cards) {
    std::vector<std::pair<Rank, int>> groups;
    for (const auto& card : cards) {
        if (!card.isValid()) continue;
        auto it = std::find_if(groups.begin(), groups.end(),
            [&card](const auto& group) { return group.first == card.rank(); });
        if (it == groups.end()) {
            groups.push_back({card.rank(), 1});
        } else {
            ++it->second;
        }
    }

    std::wstring result;
    for (size_t i = 0; i < groups.size(); ++i) {
        if (i > 0) result += L"、";
        result += formatSameRankSpeech(groups[i].first, groups[i].second);
    }
    return result;
}

std::wstring CardTextFormatter::formatPublicCards(const std::vector<Card>& cards) {
    std::vector<std::pair<Rank, int>> groups;
    for (const auto& card : cards) {
        if (!card.isValid()) continue;
        auto it = std::find_if(groups.begin(), groups.end(),
            [&card](const auto& group) { return group.first == card.rank(); });
        if (it == groups.end()) {
            groups.push_back({card.rank(), 1});
        } else {
            ++it->second;
        }
    }

    std::wstring result;
    for (size_t i = 0; i < groups.size(); ++i) {
        if (i > 0) result += L"、";
        result += groups[i].second == 1
            ? formatRankSpeech(groups[i].first)
            : formatSameRankSpeech(groups[i].first, groups[i].second);
    }
    return result;
}

std::wstring CardTextFormatter::formatHandPosition(int position, int total) {
    (void)position;
    (void)total;
    return {};
}

std::wstring CardTextFormatter::formatCardWithSelection(
    const Card& card,
    bool selected,
    int position,
    int total,
    int sameRankCount) {
    (void)selected;
    (void)position;
    (void)total;
    if (!card.isValid()) return L"无效牌";
    return formatSameRankSpeech(card.rank(), sameRankCount);
}

} // namespace fpdz
