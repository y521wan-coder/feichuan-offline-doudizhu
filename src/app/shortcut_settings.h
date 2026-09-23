#pragma once

#include <QJsonObject>
#include <QString>
#include <array>
#include <optional>

class QKeySequence;

namespace fpdz {

enum class ShortcutAction {
    Battle = 0,
    BottomCards,
    OpenSettings,
    PauseResume,
    ExitApplication,
    QuickExitApplication,
    CurrentTurn,
    LastAction,
    Score,
    ReturnToMainScreen,
    PreviousRankGroup,
    NextRankGroup,
    FirstRankGroup,
    LastRankGroup,
    PickCard,
    PickRankGroup,
    PutDownCard,
    PutDownAllCards,
    PlayCards,
    Pass,
    BidZero,
    PlayerOneOrBidOne,
    PlayerTwoOrBidTwo,
    PlayerThreeOrBidThree,
    PlayerFour,
    Count
};

struct ShortcutBinding {
    int key = 0;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;

    bool operator==(const ShortcutBinding& other) const {
        return key == other.key && modifiers == other.modifiers;
    }
    bool operator!=(const ShortcutBinding& other) const { return !(*this == other); }
};

class ShortcutSettings {
public:
    static constexpr std::size_t ActionCount =
        static_cast<std::size_t>(ShortcutAction::Count);

    ShortcutSettings();

    const ShortcutBinding& binding(ShortcutAction action) const;
    void setBinding(ShortcutAction action, const ShortcutBinding& binding);
    std::optional<ShortcutAction> actionFor(const ShortcutBinding& binding) const;
    std::optional<ShortcutAction> conflictingAction(
        ShortcutAction action, const ShortcutBinding& binding) const;

    void resetToDefaults();
    void normalize();
    QJsonObject toJson() const;
    static ShortcutSettings fromJson(const QJsonObject& json);

    static ShortcutBinding defaultBinding(ShortcutAction action);
    static QString actionId(ShortcutAction action);
    static QString actionName(ShortcutAction action);
    static QString keyName(const ShortcutBinding& binding);
    static bool isValidBinding(const ShortcutBinding& binding);
    static QKeySequence keySequence(const ShortcutBinding& binding);
    static ShortcutBinding fromKeyEvent(int key, Qt::KeyboardModifiers modifiers);

private:
    std::array<ShortcutBinding, ActionCount> m_bindings{};
};

} // namespace fpdz
