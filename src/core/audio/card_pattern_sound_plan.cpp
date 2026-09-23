#include "card_pattern_sound_plan.h"

#include <array>

namespace fpdz {
namespace {

std::string rankStem(Rank rank) {
    switch (rank) {
    case Rank::Three:      return "3";
    case Rank::Four:       return "4";
    case Rank::Five:       return "5";
    case Rank::Six:        return "6";
    case Rank::Seven:      return "7";
    case Rank::Eight:      return "8";
    case Rank::Nine:       return "9";
    case Rank::Ten:        return "10";
    case Rank::Jack:       return "J";
    case Rank::Queen:      return "Q";
    case Rank::King:       return "K";
    case Rank::Ace:        return "A";
    case Rank::Two:        return "2";
    case Rank::SmallJoker: return "SmallKing";
    case Rank::BigJoker:   return "BigKing";
    }
    return {};
}

std::string voiceFile(PlayerId playerId, const std::string& stem,
                      bool humanUsesFemaleVoice) {
    const int index = static_cast<int>(playerId);
    const auto directory = (index == 0 && humanUsesFemaleVoice) || index == 3
        ? "girl" : "boy";
    return "card_four/" + std::string(directory) + "/" + stem + ".wav";
}

void appendVoice(std::vector<std::string>& files, PlayerId playerId,
                 const std::string& stem, bool humanUsesFemaleVoice) {
    if (!stem.empty()) files.push_back(voiceFile(playerId, stem, humanUsesFemaleVoice));
}

void appendBodyRange(std::vector<std::string>& files, const GameEvent& event,
                     bool humanUsesFemaleVoice) {
    const int start = rankWeight(event.pattern.mainRank);
    const int end = start + event.pattern.mainLength - 1;
    appendVoice(files, event.playerId, rankStem(static_cast<Rank>(start)),
                humanUsesFemaleVoice);
    appendVoice(files, event.playerId, "zhi", humanUsesFemaleVoice);
    appendVoice(files, event.playerId, rankStem(static_cast<Rank>(end)),
                humanUsesFemaleVoice);
}

std::array<int, RANK_COUNT> remainingCounts(const GameEvent& event, int bodyCopies) {
    std::array<int, RANK_COUNT> counts{};
    for (const auto& card : event.cards) {
        if (card.isValid()) ++counts[static_cast<size_t>(card.rank())];
    }
    const int start = rankWeight(event.pattern.mainRank);
    for (int offset = 0; offset < event.pattern.mainLength; ++offset) {
        counts[static_cast<size_t>(start + offset)] -= bodyCopies;
    }
    return counts;
}

void appendRemainingPairs(std::vector<std::string>& files, const GameEvent& event,
                          int bodyCopies, bool humanUsesFemaleVoice) {
    const auto counts = remainingCounts(event, bodyCopies);
    const std::array<Rank, RANK_COUNT> speechOrder = {
        Rank::Two, Rank::Three, Rank::Four, Rank::Five, Rank::Six,
        Rank::Seven, Rank::Eight, Rank::Nine, Rank::Ten, Rank::Jack,
        Rank::Queen, Rank::King, Rank::Ace, Rank::SmallJoker, Rank::BigJoker
    };
    for (const auto rank : speechOrder) {
        const int value = static_cast<int>(rank);
        if (counts[static_cast<size_t>(value)] == 2) {
            appendVoice(files, event.playerId,
                        "pair" + rankStem(rank), humanUsesFemaleVoice);
        }
    }
}

void appendRemainingSingles(std::vector<std::string>& files, const GameEvent& event,
                            int bodyCopies, bool humanUsesFemaleVoice) {
    const auto counts = remainingCounts(event, bodyCopies);
    for (int value = 0; value < RANK_COUNT; ++value) {
        for (int copy = 0; copy < counts[static_cast<size_t>(value)]; ++copy) {
            appendVoice(files, event.playerId, rankStem(static_cast<Rank>(value)),
                        humanUsesFemaleVoice);
        }
    }
}

} // namespace

CardPatternSoundPlan buildCardPatternSoundPlan(
    const GameEvent& event, bool humanUsesFemaleVoice) {
    CardPatternSoundPlan plan;
    auto& files = plan.voiceFiles;
    const auto rank = rankStem(event.pattern.mainRank);

    switch (event.pattern.type) {
    case CardPatternType::Single:
        appendVoice(files, event.playerId, rank, humanUsesFemaleVoice);
        break;
    case CardPatternType::Pair:
        appendVoice(files, event.playerId, "pair" + rank, humanUsesFemaleVoice);
        break;
    case CardPatternType::Triple:
        appendVoice(files, event.playerId, "three", humanUsesFemaleVoice);
        appendVoice(files, event.playerId, rank, humanUsesFemaleVoice);
        break;
    case CardPatternType::TripleWithSingle:
        appendVoice(files, event.playerId, "three", humanUsesFemaleVoice);
        appendVoice(files, event.playerId, rank, humanUsesFemaleVoice);
        appendVoice(files, event.playerId, "to", humanUsesFemaleVoice);
        appendRemainingSingles(files, event, 3, humanUsesFemaleVoice);
        break;
    case CardPatternType::TripleWithPair:
        appendVoice(files, event.playerId, "three", humanUsesFemaleVoice);
        appendVoice(files, event.playerId, rank, humanUsesFemaleVoice);
        appendVoice(files, event.playerId, "to", humanUsesFemaleVoice);
        appendRemainingPairs(files, event, 3, humanUsesFemaleVoice);
        break;
    case CardPatternType::Straight:
        appendBodyRange(files, event, humanUsesFemaleVoice);
        appendVoice(files, event.playerId, "line", humanUsesFemaleVoice);
        plan.effectFile = "card_four/shunzi.wav";
        break;
    case CardPatternType::ConsecutivePairs:
        appendBodyRange(files, event, humanUsesFemaleVoice);
        appendVoice(files, event.playerId, "linkPair", humanUsesFemaleVoice);
        plan.effectFile = "card_four/liandui.wav";
        break;
    case CardPatternType::Airplane:
        appendBodyRange(files, event, humanUsesFemaleVoice);
        appendVoice(files, event.playerId, "linkThree", humanUsesFemaleVoice);
        break;
    case CardPatternType::AirplaneWithPairs:
        appendBodyRange(files, event, humanUsesFemaleVoice);
        appendVoice(files, event.playerId, "plane", humanUsesFemaleVoice);
        appendRemainingPairs(files, event, 3, humanUsesFemaleVoice);
        break;
    case CardPatternType::AirplaneWithSingles:
        appendBodyRange(files, event, humanUsesFemaleVoice);
        appendVoice(files, event.playerId, "plane", humanUsesFemaleVoice);
        appendRemainingSingles(files, event, 3, humanUsesFemaleVoice);
        break;
    case CardPatternType::FourWithTwoSingles:
        appendVoice(files, event.playerId, "fourWithOne", humanUsesFemaleVoice);
        appendVoice(files, event.playerId, rank, humanUsesFemaleVoice);
        appendRemainingSingles(files, event, 4, humanUsesFemaleVoice);
        break;
    case CardPatternType::FourWithTwoPairs:
        appendVoice(files, event.playerId, "fourWithTwoPairs", humanUsesFemaleVoice);
        appendVoice(files, event.playerId, rank, humanUsesFemaleVoice);
        appendRemainingPairs(files, event, 4, humanUsesFemaleVoice);
        break;
    case CardPatternType::Gun:
        appendVoice(files, event.playerId, rank, humanUsesFemaleVoice);
        appendVoice(files, event.playerId, "qiangbi", humanUsesFemaleVoice);
        plan.effectFile = "card_four/qiangbi.wav";
        break;
    case CardPatternType::KingBomb:
        appendVoice(files, event.playerId, "shuangwangqiangbi", humanUsesFemaleVoice);
        plan.effectFile = "card_four/qiangbi.wav";
        break;
    case CardPatternType::Cannon:
        appendVoice(files, event.playerId, rank, humanUsesFemaleVoice);
        appendVoice(files, event.playerId, "paohong", humanUsesFemaleVoice);
        plan.effectFile = "card_four/paohong.wav";
        break;
    case CardPatternType::Rocket:
        appendVoice(files, event.playerId, rank, humanUsesFemaleVoice);
        appendVoice(files, event.playerId, "huojian", humanUsesFemaleVoice);
        plan.effectFile = "card_four/huojian.wav";
        break;
    case CardPatternType::Missile:
        appendVoice(files, event.playerId, rank, humanUsesFemaleVoice);
        appendVoice(files, event.playerId, "daodan", humanUsesFemaleVoice);
        plan.effectFile = "card_four/daodan.wav";
        break;
    case CardPatternType::SkyBlast:
        appendVoice(files, event.playerId, rank, humanUsesFemaleVoice);
        appendVoice(files, event.playerId, "tianzha", humanUsesFemaleVoice);
        plan.effectFile = "card_four/tianzha.wav";
        break;
    case CardPatternType::HeavenlyLord:
        appendVoice(files, event.playerId, "tianzun", humanUsesFemaleVoice);
        plan.effectFile = "card_four/tianzun.wav";
        break;
    case CardPatternType::Invalid:
        break;
    }

    return plan;
}

} // namespace fpdz
