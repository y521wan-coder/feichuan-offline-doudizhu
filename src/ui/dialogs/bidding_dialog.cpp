#include "bidding_dialog.h"
#include <QDialogButtonBox>

namespace fpdz {

BiddingDialog::BiddingDialog(int currentHighestBid, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QString::fromStdWString(L"叫分"));
    
    auto* layout = new QVBoxLayout(this);
    
    auto* label = new QLabel(QString::fromStdWString(L"请选择叫分（当前最高：" + 
        std::to_wstring(currentHighestBid) + L"分）"), this);
    layout->addWidget(label);
    
    m_buttonGroup = new QButtonGroup(this);
    
    // 不叫
    auto* passButton = new QRadioButton(QString::fromStdWString(L"不叫 (0分)"), this);
    passButton->setChecked(true);
    m_buttonGroup->addButton(passButton, 0);
    layout->addWidget(passButton);
    
    // 1分、2分、3分（必须高于当前最高分）
    for (int i = 1; i <= 3; ++i) {
        if (i > currentHighestBid) {
            auto* button = new QRadioButton(QString::fromStdWString(std::to_wstring(i) + L"分"), this);
            m_buttonGroup->addButton(button, i);
            layout->addWidget(button);
        }
    }
    
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &BiddingDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void BiddingDialog::onAccept() {
    m_bidValue = m_buttonGroup->checkedId();
    accept();
}

} // namespace fpdz
