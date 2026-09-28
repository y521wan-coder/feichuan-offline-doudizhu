#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPushButton>
#include <algorithm>
#include <vector>

namespace fpdz {

class OnlineButton final : public QPushButton {
public:
    using QPushButton::QPushButton;

protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (!event->isAutoRepeat()) click();
            event->accept();
            return;
        }
        QPushButton::keyPressEvent(event);
    }
};

inline std::vector<QJsonObject> sortedLobbyRooms(const QJsonArray& rooms, int playerCount) {
    std::vector<QJsonObject> sorted;
    sorted.reserve(rooms.size());
    for (const QJsonValue& value : rooms) {
        const QJsonObject summary = value.toObject();
        if (summary.value(QStringLiteral("player_count")).toInt() == playerCount)
            sorted.push_back(summary);
    }
    std::sort(sorted.begin(), sorted.end(), [](const QJsonObject& lhs, const QJsonObject& rhs) {
        const auto number = [](const QJsonObject& room) {
            return room.value(QStringLiteral("room_id")).toString()
                .section(QLatin1Char('-'), 1).toInt();
        };
        return number(lhs) < number(rhs);
    });
    return sorted;
}

} // namespace fpdz
