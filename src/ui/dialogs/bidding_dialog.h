#pragma once
#include <QDialog>
#include <QButtonGroup>
#include <QRadioButton>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>

namespace fpdz {

class BiddingDialog : public QDialog {
    Q_OBJECT
public:
    explicit BiddingDialog(int currentHighestBid, QWidget* parent = nullptr);
    int getBidValue() const { return m_bidValue; }

private slots:
    void onAccept();

private:
    int m_bidValue = 0;
    QButtonGroup* m_buttonGroup = nullptr;
};

} // namespace fpdz
