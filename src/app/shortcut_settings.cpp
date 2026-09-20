#include "shortcut_settings.h"

#include <QKeySequence>
#include <QJsonValue>
#include <QSet>
#include <QStringList>

namespace fpdz {

namespace {

constexpr std::size_t indexOf(ShortcutAction action) {
    return static_cast<std::size_t>(action);
}

Qt::KeyboardModifiers normalizedModifiers(Qt::KeyboardModifiers modifiers) {
    return modifiers & (Qt::ControlModifier | Qt::ShiftModifier |
                        Qt::AltModifier | Qt::KeypadModifier);
}

QString baseKeyName(int key, bool keypad) {
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return QString::fromUtf8(u8"字母%1键").arg(QChar(u'A' + key - Qt::Key_A));
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return keypad
            ? QString::fromUtf8(u8"小键盘%1键").arg(key - Qt::Key_0)
            : QString::fromUtf8(u8"数字%1键").arg(key - Qt::Key_0);
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F35) {
        return QStringLiteral("F%1").arg(key - Qt::Key_F1 + 1);
    }

    switch (key) {
    case Qt::Key_Left: return QString::fromUtf8(u8"左光标键");
    case Qt::Key_Right: return QString::fromUtf8(u8"右光标键");
    case Qt::Key_Up: return QString::fromUtf8(u8"上光标键");
    case Qt::Key_Down: return QString::fromUtf8(u8"下光标键");
    case Qt::Key_Home: return QString::fromUtf8(u8"行首键");
    case Qt::Key_End: return QString::fromUtf8(u8"行尾键");
    case Qt::Key_PageUp: return QString::fromUtf8(u8"上翻页键");
    case Qt::Key_PageDown: return QString::fromUtf8(u8"下翻页键");
    case Qt::Key_Insert: return QString::fromUtf8(u8"插入键");
    case Qt::Key_Delete: return QString::fromUtf8(u8"删除键");
    case Qt::Key_Backspace: return QString::fromUtf8(u8"退格键");
    case Qt::Key_Return:
    case Qt::Key_Enter: return QString::fromUtf8(u8"回车键");
    case Qt::Key_Space: return QString::fromUtf8(u8"空格键");
    case Qt::Key_Escape: return QString::fromUtf8(u8"退出键");
    case Qt::Key_Tab: return QString::fromUtf8(u8"跳格键");
    case Qt::Key_CapsLock: return QString::fromUtf8(u8"大写锁定键");
    case Qt::Key_NumLock: return QString::fromUtf8(u8"数字锁定键");
    case Qt::Key_ScrollLock: return QString::fromUtf8(u8"滚动锁定键");
    case Qt::Key_Pause: return QString::fromUtf8(u8"暂停键");
    case Qt::Key_Print: return QString::fromUtf8(u8"打印屏幕键");
    case Qt::Key_Minus: return QString::fromUtf8(u8"减号键");
    case Qt::Key_Equal: return QString::fromUtf8(u8"等号键");
    case Qt::Key_Comma: return QString::fromUtf8(u8"逗号键");
    case Qt::Key_Period: return QString::fromUtf8(u8"句号键");
    case Qt::Key_Slash: return QString::fromUtf8(u8"斜杠键");
    case Qt::Key_Backslash: return QString::fromUtf8(u8"反斜杠键");
    case Qt::Key_Semicolon: return QString::fromUtf8(u8"分号键");
    case Qt::Key_Apostrophe: return QString::fromUtf8(u8"单引号键");
    case Qt::Key_BracketLeft: return QString::fromUtf8(u8"左方括号键");
    case Qt::Key_BracketRight: return QString::fromUtf8(u8"右方括号键");
    case Qt::Key_QuoteLeft: return QString::fromUtf8(u8"反引号键");
    case Qt::Key_Asterisk: return QString::fromUtf8(u8"星号键");
    case Qt::Key_Plus: return QString::fromUtf8(u8"加号键");
    default: return {};
    }
}

} // namespace

ShortcutSettings::ShortcutSettings() {
    resetToDefaults();
}

const ShortcutBinding& ShortcutSettings::binding(ShortcutAction action) const {
    return m_bindings[indexOf(action)];
}

void ShortcutSettings::setBinding(ShortcutAction action, const ShortcutBinding& binding) {
    m_bindings[indexOf(action)] = fromKeyEvent(binding.key, binding.modifiers);
}

std::optional<ShortcutAction> ShortcutSettings::actionFor(
    const ShortcutBinding& candidate) const {
    const ShortcutBinding normalized = fromKeyEvent(candidate.key, candidate.modifiers);
    for (std::size_t i = 0; i < ActionCount; ++i) {
        if (m_bindings[i] == normalized) return static_cast<ShortcutAction>(i);
    }
    return std::nullopt;
}

std::optional<ShortcutAction> ShortcutSettings::conflictingAction(
    ShortcutAction action, const ShortcutBinding& candidate) const {
    const auto conflict = actionFor(candidate);
    if (conflict && *conflict != action) return conflict;
    return std::nullopt;
}

void ShortcutSettings::resetToDefaults() {
    for (std::size_t i = 0; i < ActionCount; ++i) {
        m_bindings[i] = defaultBinding(static_cast<ShortcutAction>(i));
    }
}

void ShortcutSettings::normalize() {
    QSet<quint64> used;
    bool invalid = false;
    for (std::size_t i = 0; i < ActionCount; ++i) {
        m_bindings[i] = fromKeyEvent(m_bindings[i].key, m_bindings[i].modifiers);
        const quint64 identity = (static_cast<quint64>(static_cast<quint32>(m_bindings[i].key)) << 32) |
            static_cast<quint32>(m_bindings[i].modifiers.toInt());
        if (!isValidBinding(m_bindings[i]) || used.contains(identity)) {
            invalid = true;
            break;
        }
        used.insert(identity);
    }
    if (invalid) resetToDefaults();
}

QJsonObject ShortcutSettings::toJson() const {
    ShortcutSettings normalized = *this;
    normalized.normalize();
    QJsonObject json;
    for (std::size_t i = 0; i < ActionCount; ++i) {
        const auto action = static_cast<ShortcutAction>(i);
        const auto& value = normalized.m_bindings[i];
        QJsonObject bindingJson;
        bindingJson.insert(QStringLiteral("key"), value.key);
        bindingJson.insert(QStringLiteral("modifiers"), value.modifiers.toInt());
        json.insert(actionId(action), bindingJson);
    }
    return json;
}

ShortcutSettings ShortcutSettings::fromJson(const QJsonObject& json) {
    ShortcutSettings settings;
    for (std::size_t i = 0; i < ActionCount; ++i) {
        const auto action = static_cast<ShortcutAction>(i);
        const QJsonValue value = json.value(actionId(action));
        if (!value.isObject()) continue;
        const QJsonObject object = value.toObject();
        if (!object.value(QStringLiteral("key")).isDouble() ||
            !object.value(QStringLiteral("modifiers")).isDouble()) {
            continue;
        }
        settings.m_bindings[i] = {
            object.value(QStringLiteral("key")).toInt(),
            Qt::KeyboardModifiers(object.value(QStringLiteral("modifiers")).toInt())};
    }
    settings.normalize();
    return settings;
}

ShortcutBinding ShortcutSettings::defaultBinding(ShortcutAction action) {
    using enum ShortcutAction;
    switch (action) {
    case Battle: return {Qt::Key_F1, Qt::NoModifier};
    case BottomCards: return {Qt::Key_F2, Qt::NoModifier};
    case OpenSettings: return {Qt::Key_F5, Qt::NoModifier};
    case PauseResume: return {Qt::Key_P, Qt::ControlModifier};
    case ExitApplication: return {Qt::Key_Q, Qt::ControlModifier};
    case QuickExitApplication: return {Qt::Key_X, Qt::AltModifier};
    case CurrentTurn: return {Qt::Key_F11, Qt::NoModifier};
    case LastAction: return {Qt::Key_F12, Qt::NoModifier};
    case Score: return {Qt::Key_F, Qt::AltModifier};
    case ReturnToMainScreen: return {Qt::Key_Escape, Qt::NoModifier};
    case PreviousRankGroup: return {Qt::Key_Left, Qt::NoModifier};
    case NextRankGroup: return {Qt::Key_Right, Qt::NoModifier};
    case PreviousCard: return {Qt::Key_Left, Qt::ShiftModifier};
    case NextCard: return {Qt::Key_Right, Qt::ShiftModifier};
    case FirstRankGroup: return {Qt::Key_Home, Qt::NoModifier};
    case LastRankGroup: return {Qt::Key_End, Qt::NoModifier};
    case PickCard: return {Qt::Key_Up, Qt::NoModifier};
    case PickRankGroup: return {Qt::Key_Up, Qt::ControlModifier};
    case PutDownCard: return {Qt::Key_Down, Qt::NoModifier};
    case PutDownAllCards: return {Qt::Key_Down, Qt::ControlModifier};
    case PlayCards: return {Qt::Key_Return, Qt::NoModifier};
    case Pass: return {Qt::Key_Return, Qt::ControlModifier};
    case BidZero: return {Qt::Key_0, Qt::NoModifier};
    case PlayerOneOrBidOne: return {Qt::Key_1, Qt::NoModifier};
    case PlayerTwoOrBidTwo: return {Qt::Key_2, Qt::NoModifier};
    case PlayerThreeOrBidThree: return {Qt::Key_3, Qt::NoModifier};
    case PlayerFour: return {Qt::Key_4, Qt::NoModifier};
    case Count: break;
    }
    return {};
}

QString ShortcutSettings::actionId(ShortcutAction action) {
    using enum ShortcutAction;
    switch (action) {
    case Battle: return QStringLiteral("battle");
    case BottomCards: return QStringLiteral("bottomCards");
    case OpenSettings: return QStringLiteral("openSettings");
    case PauseResume: return QStringLiteral("pauseResume");
    case ExitApplication: return QStringLiteral("exitApplication");
    case QuickExitApplication: return QStringLiteral("quickExitApplication");
    case CurrentTurn: return QStringLiteral("currentTurn");
    case LastAction: return QStringLiteral("lastAction");
    case Score: return QStringLiteral("score");
    case ReturnToMainScreen: return QStringLiteral("returnToMainScreen");
    case PreviousRankGroup: return QStringLiteral("previousRankGroup");
    case NextRankGroup: return QStringLiteral("nextRankGroup");
    case PreviousCard: return QStringLiteral("previousCard");
    case NextCard: return QStringLiteral("nextCard");
    case FirstRankGroup: return QStringLiteral("firstRankGroup");
    case LastRankGroup: return QStringLiteral("lastRankGroup");
    case PickCard: return QStringLiteral("pickCard");
    case PickRankGroup: return QStringLiteral("pickRankGroup");
    case PutDownCard: return QStringLiteral("putDownCard");
    case PutDownAllCards: return QStringLiteral("putDownAllCards");
    case PlayCards: return QStringLiteral("playCards");
    case Pass: return QStringLiteral("pass");
    case BidZero: return QStringLiteral("bidZero");
    case PlayerOneOrBidOne: return QStringLiteral("playerOneOrBidOne");
    case PlayerTwoOrBidTwo: return QStringLiteral("playerTwoOrBidTwo");
    case PlayerThreeOrBidThree: return QStringLiteral("playerThreeOrBidThree");
    case PlayerFour: return QStringLiteral("playerFour");
    case Count: break;
    }
    return {};
}

QString ShortcutSettings::actionName(ShortcutAction action) {
    using enum ShortcutAction;
    switch (action) {
    case Battle: return QString::fromUtf8(u8"开战、停战或恢复");
    case BottomCards: return QString::fromUtf8(u8"查询底牌");
    case OpenSettings: return QString::fromUtf8(u8"打开设置选项");
    case PauseResume: return QString::fromUtf8(u8"暂停或恢复");
    case ExitApplication: return QString::fromUtf8(u8"退出软件");
    case QuickExitApplication: return QString::fromUtf8(u8"快速退出软件");
    case CurrentTurn: return QString::fromUtf8(u8"朗读当前出牌者");
    case LastAction: return QString::fromUtf8(u8"复读上一条出牌");
    case Score: return QString::fromUtf8(u8"朗读当前分数");
    case ReturnToMainScreen: return QString::fromUtf8(u8"返回主界面");
    case PreviousRankGroup: return QString::fromUtf8(u8"浏览上一个点数组");
    case NextRankGroup: return QString::fromUtf8(u8"浏览下一个点数组");
    case PreviousCard: return QString::fromUtf8(u8"逐张浏览上一张牌");
    case NextCard: return QString::fromUtf8(u8"逐张浏览下一张牌");
    case FirstRankGroup: return QString::fromUtf8(u8"跳到第一个点数组");
    case LastRankGroup: return QString::fromUtf8(u8"跳到最后一个点数组");
    case PickCard: return QString::fromUtf8(u8"拿起当前一张牌");
    case PickRankGroup: return QString::fromUtf8(u8"拿起当前完整点数组");
    case PutDownCard: return QString::fromUtf8(u8"放下一张已拿起的牌");
    case PutDownAllCards: return QString::fromUtf8(u8"放下全部已拿起的牌");
    case PlayCards: return QString::fromUtf8(u8"出牌");
    case Pass: return QString::fromUtf8(u8"过牌");
    case BidZero: return QString::fromUtf8(u8"叫分时不叫");
    case PlayerOneOrBidOne: return QString::fromUtf8(u8"叫一分或查询玩家一");
    case PlayerTwoOrBidTwo: return QString::fromUtf8(u8"叫二分或查询玩家二");
    case PlayerThreeOrBidThree: return QString::fromUtf8(u8"叫三分或查询玩家三");
    case PlayerFour: return QString::fromUtf8(u8"查询玩家四");
    case Count: break;
    }
    return {};
}

QString ShortcutSettings::keyName(const ShortcutBinding& input) {
    const ShortcutBinding binding = fromKeyEvent(input.key, input.modifiers);
    QStringList parts;
    if (binding.modifiers.testFlag(Qt::ControlModifier)) {
        parts.append(QStringLiteral("Control"));
    }
    if (binding.modifiers.testFlag(Qt::ShiftModifier)) {
        parts.append(QStringLiteral("Shift"));
    }
    if (binding.modifiers.testFlag(Qt::AltModifier)) {
        parts.append(QStringLiteral("Alt"));
    }
    const QString key = baseKeyName(
        binding.key, binding.modifiers.testFlag(Qt::KeypadModifier));
    if (key.isEmpty()) return QString::fromUtf8(u8"无效按键");
    parts.append(key);
    return parts.join(QString::fromUtf8(u8"加"));
}

bool ShortcutSettings::isValidBinding(const ShortcutBinding& input) {
    const ShortcutBinding binding = fromKeyEvent(input.key, input.modifiers);
    if (binding.key == 0 || binding.key == Qt::Key_unknown ||
        binding.key == Qt::Key_Control || binding.key == Qt::Key_Shift ||
        binding.key == Qt::Key_Alt || binding.key == Qt::Key_Meta ||
        binding.key == Qt::Key_AltGr) {
        return false;
    }
    return !baseKeyName(binding.key,
                        binding.modifiers.testFlag(Qt::KeypadModifier)).isEmpty();
}

QKeySequence ShortcutSettings::keySequence(const ShortcutBinding& binding) {
    return QKeySequence(binding.key | binding.modifiers.toInt());
}

ShortcutBinding ShortcutSettings::fromKeyEvent(
    int key, Qt::KeyboardModifiers modifiers) {
    if (key == Qt::Key_Enter) key = Qt::Key_Return;
    if (key == Qt::Key_Backtab) {
        key = Qt::Key_Tab;
        modifiers |= Qt::ShiftModifier;
    }
    return {key, normalizedModifiers(modifiers)};
}

} // namespace fpdz
