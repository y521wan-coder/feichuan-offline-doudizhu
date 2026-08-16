#include "shortcut_dialog.h"
namespace fpdz {
ShortcutDialog::ShortcutDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QString::fromStdWString(L"快捷键设置"));
}
}
