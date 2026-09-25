#pragma once

#include "../app/game_mode.h"

#include <QWidget>

class QPushButton;

namespace fpdz {

class ModeSelectionWindow final : public QWidget {
    Q_OBJECT
public:
    explicit ModeSelectionWindow(QWidget* parent = nullptr);

    QPushButton* offlineButton() const { return m_offlineButton; }
    QPushButton* aiBattleButton() const { return m_aiBattleButton; }

signals:
    void modeSelected(fpdz::GameMode mode);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QPushButton* m_offlineButton = nullptr;
    QPushButton* m_aiBattleButton = nullptr;
};

} // namespace fpdz
