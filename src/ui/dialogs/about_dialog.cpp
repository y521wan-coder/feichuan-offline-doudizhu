#include "about_dialog.h"
#include <QVBoxLayout>
#include <QLabel>
namespace fpdz {
AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QString::fromStdWString(L"关于"));
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QString::fromStdWString(L"四人斗地主 V1.0.0\n无障碍Windows桌面游戏"), this));
}
}
