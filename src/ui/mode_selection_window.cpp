#include "mode_selection_window.h"

#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace fpdz {

ModeSelectionWindow::ModeSelectionWindow(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("modeSelectionWindow"));
    setWindowTitle(QString::fromUtf8(u8"飞船斗地主 - 选择模式"));
    setAccessibleName(QString::fromUtf8(u8"飞船斗地主模式选择"));
    setMinimumSize(560, 320);

    auto* layout = new QVBoxLayout(this);
    auto* title = new QLabel(QString::fromUtf8(u8"请选择游戏模式"), this);
    title->setObjectName(QStringLiteral("modeSelectionTitle"));
    title->setAccessibleName(title->text());
    layout->addWidget(title);

    m_offlineButton = new QPushButton(QString::fromUtf8(u8"纯单机版模式"), this);
    m_offlineButton->setObjectName(QStringLiteral("offlineModeButton"));
    m_offlineButton->setAccessibleName(
        QString::fromUtf8(u8"纯单机版模式，第一项，共两项"));
    m_offlineButton->setAccessibleDescription(
        QString::fromUtf8(u8"使用现有二点一版的本地规则和三级机器人，不会启动AI服务或联网"));
    layout->addWidget(m_offlineButton);

    m_aiBattleButton = new QPushButton(QString::fromUtf8(u8"AI对战模式"), this);
    m_aiBattleButton->setObjectName(QStringLiteral("aiBattleModeButton"));
    m_aiBattleButton->setAccessibleName(
        QString::fromUtf8(u8"AI对战模式，第二项，共两项"));
    m_aiBattleButton->setAccessibleDescription(
        QString::fromUtf8(u8"为2及之后的电脑座位配置同一个云模型"));
    layout->addWidget(m_aiBattleButton);
    layout->addStretch();

    setTabOrder(m_offlineButton, m_aiBattleButton);
    setTabOrder(m_aiBattleButton, m_offlineButton);
    m_offlineButton->installEventFilter(this);
    m_aiBattleButton->installEventFilter(this);

    connect(m_offlineButton, &QPushButton::clicked, this, [this]() {
        emit modeSelected(GameMode::Offline);
    });
    connect(m_aiBattleButton, &QPushButton::clicked, this, [this]() {
        emit modeSelected(GameMode::AiBattle);
    });

    // A screen reader announces the focused button itself. Do not also send a
    // speech announcement here: that would overlap the reader's focus speech.
    QTimer::singleShot(0, this, [this]() {
        m_offlineButton->setFocus(Qt::OtherFocusReason);
    });
}

bool ModeSelectionWindow::eventFilter(QObject* watched, QEvent* event) {
    auto* button = qobject_cast<QPushButton*>(watched);
    if (button && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            button->click();
            keyEvent->accept();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace fpdz
