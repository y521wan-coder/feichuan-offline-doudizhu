#pragma once
#include "../../app/shortcut_settings.h"
#include <QDialog>

class QListWidget;

namespace fpdz {
class ShortcutDialog : public QDialog {
    Q_OBJECT
public:
    explicit ShortcutDialog(const ShortcutSettings& settings, QWidget* parent = nullptr);
    ShortcutSettings settings() const;
private:
    void refreshList(int preferredRow = -1);
    void editCurrentShortcut();
    void restoreDefaults();
    ShortcutSettings m_settings;
    QListWidget* m_shortcutList = nullptr;
};
}
