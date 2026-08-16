#pragma once
#include <QLabel>

namespace fpdz {

class GameStatusWidget : public QLabel {
    Q_OBJECT

public:
    explicit GameStatusWidget(QWidget* parent = nullptr);
};

} // namespace fpdz
