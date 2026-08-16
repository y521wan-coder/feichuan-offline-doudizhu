#include "game_status_widget.h"
namespace fpdz {
GameStatusWidget::GameStatusWidget(QWidget* parent) : QLabel(parent) {
    setObjectName(QStringLiteral("gameStatusAnnouncement"));
}
}
