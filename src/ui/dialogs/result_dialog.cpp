#include "result_dialog.h"

#include <QListWidget>
#include <QVBoxLayout>

namespace fpdz {

ResultDialog::ResultDialog(const QStringList& items, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QString::fromUtf8(u8"结算"));
    setModal(true);
    setAttribute(Qt::WA_DeleteOnClose);

    auto* layout = new QVBoxLayout(this);
    m_resultList = new QListWidget(this);
    m_resultList->setObjectName(QStringLiteral("roundResultList"));
    m_resultList->setAccessibleName(QString::fromUtf8(u8"本局结算信息"));
    m_resultList->setAccessibleDescription(QString::fromUtf8(
        u8"使用上下光标逐项查看，按ESC返回主界面"));
    m_resultList->addItems(items);
    layout->addWidget(m_resultList);

    if (m_resultList->count() > 0) m_resultList->setCurrentRow(0);
    m_resultList->setFocus(Qt::OtherFocusReason);
}

} // namespace fpdz
