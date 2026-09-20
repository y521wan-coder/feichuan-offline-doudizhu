#include "about_dialog.h"
#include <QVBoxLayout>
#include <QLabel>
namespace fpdz {
AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QString::fromStdWString(L"关于"));
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QString::fromStdWString(L"飞船单机斗地主\n二人/三人/四人无障碍Windows桌面游戏"), this));
}
}
