#pragma once

#include <QString>

namespace fpdz {

inline QString defaultAiStrategyPrompt() {
    return QString::fromUtf8(
        u8"优先选择能立即让本队获胜的动作，其次阻止对方下一手出完；"
        u8"否则比较动作后全桌手牌，争取本队更早出完，不要只贪本手多出。"
        u8"农民通常不压队友，除非自己能立即获胜、明显加快本队获胜或必须拦截地主；"
        u8"地主争取保持出牌权并减少出完手数。谨慎使用炸弹和大牌。"
        u8"叫分时结合手牌、底牌和对手手牌判断成为地主后的胜算。");
}

constexpr int AI_MAX_STRATEGY_PROMPT_CHARS = 8192;

} // namespace fpdz
