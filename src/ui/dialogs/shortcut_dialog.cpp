#include "shortcut_dialog.h"
#include <QAccessible>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace fpdz {
namespace {

constexpr int kChoiceKindRole = Qt::UserRole;
constexpr int kChoiceKeyRole = Qt::UserRole + 1;
constexpr int kChoiceModifiersRole = Qt::UserRole + 2;

enum class ChoiceKind { BaseKey, Modifier };

class KeySelectionList final : public QListWidget {
public:
    using QListWidget::QListWidget;

protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Space && !event->isAutoRepeat() && currentItem()) {
            currentItem()->setCheckState(
                currentItem()->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
            event->accept();
            return;
        }
        QListWidget::keyPressEvent(event);
    }
};

class KeySelectionDialog final : public QDialog {
public:
    explicit KeySelectionDialog(ShortcutAction action,
                                const ShortcutBinding& currentBinding,
                                QWidget* parent = nullptr)
        : QDialog(parent) {
        setWindowTitle(QString::fromUtf8(u8"设置新快捷键"));
        setAccessibleName(windowTitle());
        setModal(true);

        auto* layout = new QVBoxLayout(this);
        auto* prompt = new QLabel(
            QString::fromUtf8(
                u8"正在修改“%1”。\n"
                u8"用上下光标键浏览按键，按空格键选中或取消。单键选择一个普通键；"
                u8"组合键可再选 Control、Shift 或 Alt。\n"
                u8"选择完成后，用跳格键切到“使用这个热键”，再按回车键确认。")
                .arg(ShortcutSettings::actionName(action)),
            this);
        prompt->setWordWrap(true);
        prompt->setAccessibleName(prompt->text());
        layout->addWidget(prompt);

        m_keyList = new KeySelectionList(this);
        m_keyList->setObjectName(QStringLiteral("shortcutKeySelectionList"));
        m_keyList->setAccessibleName(QString::fromUtf8(u8"可选择的按键，按空格键选中或取消"));
        m_keyList->setSelectionMode(QAbstractItemView::SingleSelection);
        layout->addWidget(m_keyList, 1);

        m_status = new QLabel(this);
        m_status->setObjectName(QStringLiteral("shortcutSelectionStatus"));
        m_status->setAlignment(Qt::AlignCenter);
        layout->addWidget(m_status);

        auto* buttons = new QDialogButtonBox(this);
        m_useButton = buttons->addButton(
            QString::fromUtf8(u8"使用这个热键"), QDialogButtonBox::AcceptRole);
        auto* cancelButton = buttons->addButton(
            QString::fromUtf8(u8"取消"), QDialogButtonBox::RejectRole);
        m_useButton->setObjectName(QStringLiteral("useSelectedShortcutButton"));
        cancelButton->setObjectName(QStringLiteral("cancelSelectedShortcutButton"));
        m_useButton->setAutoDefault(false);
        cancelButton->setAutoDefault(false);
        connect(m_useButton, &QPushButton::clicked, this, &QDialog::accept);
        connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
        layout->addWidget(buttons);

        addModifier(QStringLiteral("Control"), Qt::ControlModifier);
        addModifier(QStringLiteral("Shift"), Qt::ShiftModifier);
        addModifier(QStringLiteral("Alt"), Qt::AltModifier);

        for (const int key : {Qt::Key_Up, Qt::Key_Down, Qt::Key_Left, Qt::Key_Right,
                              Qt::Key_Home, Qt::Key_End, Qt::Key_PageUp, Qt::Key_PageDown,
                              Qt::Key_Insert, Qt::Key_Delete, Qt::Key_Backspace,
                              Qt::Key_Return, Qt::Key_Space, Qt::Key_Escape, Qt::Key_Tab,
                              Qt::Key_CapsLock, Qt::Key_NumLock, Qt::Key_ScrollLock,
                              Qt::Key_Pause, Qt::Key_Print}) {
            addBaseKey(key);
        }
        for (int key = Qt::Key_F1; key <= Qt::Key_F35; ++key) addBaseKey(key);
        for (int key = Qt::Key_0; key <= Qt::Key_9; ++key) addBaseKey(key);
        for (int key = Qt::Key_A; key <= Qt::Key_Z; ++key) addBaseKey(key);
        for (const int key : {Qt::Key_Minus, Qt::Key_Equal, Qt::Key_Comma,
                              Qt::Key_Period, Qt::Key_Slash, Qt::Key_Backslash,
                              Qt::Key_Semicolon, Qt::Key_Apostrophe,
                              Qt::Key_BracketLeft, Qt::Key_BracketRight,
                              Qt::Key_QuoteLeft, Qt::Key_Asterisk, Qt::Key_Plus}) {
            addBaseKey(key);
        }
        for (int key = Qt::Key_0; key <= Qt::Key_9; ++key) {
            addBaseKey(key, Qt::KeypadModifier);
        }

        selectBinding(currentBinding);
        connect(m_keyList, &QListWidget::itemChanged,
                this, [this](QListWidgetItem* item) { selectionChanged(item); });
        updateBindingAndStatus();
        m_keyList->setFocus(Qt::OtherFocusReason);
        resize(560, 650);
    }

    ShortcutBinding binding() const { return m_binding; }

private:
    void addModifier(const QString& name, Qt::KeyboardModifier modifier) {
        auto* item = new QListWidgetItem(name, m_keyList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setData(kChoiceKindRole, static_cast<int>(ChoiceKind::Modifier));
        item->setData(kChoiceKeyRole, 0);
        item->setData(kChoiceModifiersRole, static_cast<int>(modifier));
        item->setToolTip(QString::fromUtf8(u8"%1，按空格键选中或取消").arg(name));
    }

    void addBaseKey(int key, Qt::KeyboardModifiers inherentModifiers = Qt::NoModifier) {
        const ShortcutBinding binding{key, inherentModifiers};
        const QString name = ShortcutSettings::keyName(binding);
        auto* item = new QListWidgetItem(name, m_keyList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setData(kChoiceKindRole, static_cast<int>(ChoiceKind::BaseKey));
        item->setData(kChoiceKeyRole, key);
        item->setData(kChoiceModifiersRole, inherentModifiers.toInt());
        item->setToolTip(QString::fromUtf8(u8"%1，按空格键选中或取消").arg(name));
    }

    void selectBinding(const ShortcutBinding& input) {
        const ShortcutBinding binding = ShortcutSettings::fromKeyEvent(
            input.key, input.modifiers);
        int selectedBaseRow = -1;
        const QSignalBlocker blocker(m_keyList);
        for (int row = 0; row < m_keyList->count(); ++row) {
            auto* item = m_keyList->item(row);
            const auto kind = static_cast<ChoiceKind>(item->data(kChoiceKindRole).toInt());
            const auto choiceModifiers = Qt::KeyboardModifiers(
                item->data(kChoiceModifiersRole).toInt());
            bool selected = false;
            if (kind == ChoiceKind::Modifier) {
                selected = binding.modifiers.testFlag(
                    static_cast<Qt::KeyboardModifier>(choiceModifiers.toInt()));
            } else {
                selected = item->data(kChoiceKeyRole).toInt() == binding.key &&
                    binding.modifiers.testFlag(Qt::KeypadModifier) ==
                        choiceModifiers.testFlag(Qt::KeypadModifier);
                if (selected) selectedBaseRow = row;
            }
            item->setCheckState(selected ? Qt::Checked : Qt::Unchecked);
        }
        m_keyList->setCurrentRow(selectedBaseRow >= 0 ? selectedBaseRow : 0);
    }

    void selectionChanged(QListWidgetItem* changedItem) {
        if (m_updating || !changedItem) return;
        const auto kind = static_cast<ChoiceKind>(
            changedItem->data(kChoiceKindRole).toInt());
        if (kind == ChoiceKind::BaseKey && changedItem->checkState() == Qt::Checked) {
            m_updating = true;
            const QSignalBlocker blocker(m_keyList);
            for (int row = 0; row < m_keyList->count(); ++row) {
                auto* item = m_keyList->item(row);
                if (item != changedItem &&
                    static_cast<ChoiceKind>(item->data(kChoiceKindRole).toInt()) ==
                        ChoiceKind::BaseKey) {
                    item->setCheckState(Qt::Unchecked);
                }
            }
            m_updating = false;
        }
        updateBindingAndStatus();
    }

    void updateBindingAndStatus() {
        int key = 0;
        Qt::KeyboardModifiers modifiers = Qt::NoModifier;
        for (int row = 0; row < m_keyList->count(); ++row) {
            const auto* item = m_keyList->item(row);
            if (item->checkState() != Qt::Checked) continue;
            const auto kind = static_cast<ChoiceKind>(item->data(kChoiceKindRole).toInt());
            const auto choiceModifiers = Qt::KeyboardModifiers(
                item->data(kChoiceModifiersRole).toInt());
            if (kind == ChoiceKind::Modifier) {
                modifiers |= choiceModifiers;
            } else {
                key = item->data(kChoiceKeyRole).toInt();
                modifiers |= choiceModifiers;
            }
        }
        m_binding = ShortcutSettings::fromKeyEvent(key, modifiers);
        const bool valid = ShortcutSettings::isValidBinding(m_binding);
        const QString text = valid
            ? QString::fromUtf8(u8"当前选择：%1").arg(ShortcutSettings::keyName(m_binding))
            : QString::fromUtf8(u8"请至少选择一个普通按键");
        m_status->setText(text);
        m_status->setAccessibleName(text);
        m_useButton->setEnabled(valid);
        m_useButton->setAccessibleDescription(
            valid ? QString::fromUtf8(u8"确认使用%1")
                        .arg(ShortcutSettings::keyName(m_binding))
                  : QString());
        QAccessibleEvent accessibleEvent(m_status, QAccessible::Focus);
        QAccessible::updateAccessibility(&accessibleEvent);
    }

    KeySelectionList* m_keyList = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_useButton = nullptr;
    ShortcutBinding m_binding;
    bool m_updating = false;
};

} // namespace

ShortcutDialog::ShortcutDialog(const ShortcutSettings& settings, QWidget* parent)
    : QDialog(parent), m_settings(settings) {
    setWindowTitle(QString::fromUtf8(u8"快捷键设置"));
    setAccessibleName(windowTitle());
    setModal(true);
    auto* layout = new QVBoxLayout(this);
    auto* instructions = new QLabel(
        QString::fromUtf8(u8"用上下光标键浏览功能，按回车修改所选快捷键。"
                          u8"在按键列表中继续用上下光标键浏览、按空格键选中，"
                          u8"可选择一个普通键，或同时选择 Control、Shift、Alt 组成组合键。"
                          u8"修改完成后按跳格键切到“确定”，再按回车保存。"),
        this);
    instructions->setWordWrap(true);
    instructions->setAccessibleName(instructions->text());
    layout->addWidget(instructions);
    m_shortcutList = new QListWidget(this);
    m_shortcutList->setObjectName(QStringLiteral("shortcutSettingsList"));
    m_shortcutList->setAccessibleName(QString::fromUtf8(u8"全部快捷键"));
    m_shortcutList->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_shortcutList, 1);
    auto* buttons = new QDialogButtonBox(this);
    auto* defaults = buttons->addButton(
        QString::fromUtf8(u8"恢复全部默认值"), QDialogButtonBox::ResetRole);
    auto* ok = buttons->addButton(QDialogButtonBox::Ok);
    auto* cancel = buttons->addButton(QDialogButtonBox::Cancel);
    ok->setText(QString::fromUtf8(u8"确定"));
    cancel->setText(QString::fromUtf8(u8"取消"));
    ok->setObjectName(QStringLiteral("saveShortcutSettingsButton"));
    cancel->setObjectName(QStringLiteral("cancelShortcutSettingsButton"));
    defaults->setObjectName(QStringLiteral("resetShortcutSettingsButton"));
    ok->setAutoDefault(false);
    cancel->setAutoDefault(false);
    defaults->setAutoDefault(false);
    layout->addWidget(buttons);
    connect(m_shortcutList, &QListWidget::itemActivated,
            this, [this](QListWidgetItem*) { editCurrentShortcut(); });
    connect(defaults, &QPushButton::clicked,
            this, &ShortcutDialog::restoreDefaults);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    refreshList(0);
    resize(620, 650);
}

ShortcutSettings ShortcutDialog::settings() const { return m_settings; }

void ShortcutDialog::refreshList(int preferredRow) {
    if (!m_shortcutList) return;
    const int row = preferredRow >= 0 ? preferredRow : m_shortcutList->currentRow();
    m_shortcutList->clear();
    for (std::size_t i = 0; i < ShortcutSettings::ActionCount; ++i) {
        const auto action = static_cast<ShortcutAction>(i);
        const QString text = QString::fromUtf8(u8"%1，当前按键：%2")
            .arg(ShortcutSettings::actionName(action),
                 ShortcutSettings::keyName(m_settings.binding(action)));
        auto* item = new QListWidgetItem(text, m_shortcutList);
        item->setData(Qt::UserRole, static_cast<int>(action));
        item->setToolTip(text);
    }
    if (m_shortcutList->count() > 0) {
        m_shortcutList->setCurrentRow(qBound(0, row, m_shortcutList->count() - 1));
        m_shortcutList->setFocus(Qt::OtherFocusReason);
    }
}

void ShortcutDialog::editCurrentShortcut() {
    if (!m_shortcutList || !m_shortcutList->currentItem()) return;
    const int row = m_shortcutList->currentRow();
    const auto action = static_cast<ShortcutAction>(
        m_shortcutList->currentItem()->data(Qt::UserRole).toInt());
    KeySelectionDialog selection(action, m_settings.binding(action), this);
    if (selection.exec() != QDialog::Accepted) {
        m_shortcutList->setFocus(Qt::OtherFocusReason);
        return;
    }
    const ShortcutBinding candidate = selection.binding();
    if (const auto conflict = m_settings.conflictingAction(action, candidate)) {
        QMessageBox::warning(
            this, QString::fromUtf8(u8"快捷键冲突"),
            QString::fromUtf8(u8"“%1”已经使用“%2”。请为“%3”换一个按键。")
                .arg(ShortcutSettings::actionName(*conflict),
                     ShortcutSettings::keyName(candidate),
                     ShortcutSettings::actionName(action)));
        m_shortcutList->setFocus(Qt::OtherFocusReason);
        return;
    }
    m_settings.setBinding(action, candidate);
    refreshList(row);
}

void ShortcutDialog::restoreDefaults() {
    const int row = m_shortcutList ? m_shortcutList->currentRow() : 0;
    m_settings.resetToDefaults();
    refreshList(row);
}
}
