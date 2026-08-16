#pragma once
#include "../model/card.h"
#include "../model/hand.h"
#include "../rules/card_pattern.h"
#include <string>
#include <vector>

namespace fpdz {
class CardTextFormatter {
public:
    static std::wstring formatCard(const Card& card);
    static std::wstring formatCards(const std::vector<Card>& cards);
    static std::wstring formatPublicCards(const std::vector<Card>& cards);
    static std::wstring formatRankSpeech(Rank rank);
    static std::wstring formatSameRankSpeech(Rank rank, int count);
    static std::wstring formatPlayedCards(const CardPattern& pattern,
                                          const std::vector<Card>& cards);
    static std::wstring formatHandPosition(int position, int total);
    static std::wstring formatCardWithSelection(const Card& card, bool selected, int position, int total, int sameRankCount);
};
} // namespace fpdz
