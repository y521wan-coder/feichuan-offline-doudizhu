#pragma once

#include <QDialog>
#include <QStringList>

class QListWidget;

namespace fpdz {

class ResultDialog : public QDialog {
    Q_OBJECT
public:
    explicit ResultDialog(const QStringList& items, QWidget* parent = nullptr);

    QListWidget* resultList() const { return m_resultList; }

private:
    QListWidget* m_resultList = nullptr;
};

} // namespace fpdz
