#include "online_lobby_controls.h"

#include <QSignalSpy>
#include <QTest>

namespace fpdz {

class OnlineLobbyTest final : public QObject {
    Q_OBJECT

private slots:
    void roomsFollowNumberOrder() {
        QJsonArray mixed;
        for (int count : {2, 3, 4}) {
            for (int number = 20; number >= 1; --number)
                mixed.append(QJsonObject{{QStringLiteral("player_count"), count},
                    {QStringLiteral("room_id"), QStringLiteral("%1-%2").arg(count)
                        .arg(number, 2, 10, QLatin1Char('0'))}});
        }
        for (int count : {2, 3, 4}) {
            const auto rooms = sortedLobbyRooms(mixed, count);
            QCOMPARE(rooms.size(), 20u);
            for (int index = 0; index < 20; ++index)
                QCOMPARE(rooms[index].value(QStringLiteral("room_id")).toString(),
                    QStringLiteral("%1-%2").arg(count)
                        .arg(index + 1, 2, 10, QLatin1Char('0')));
        }
    }

    void onlineButtonsAcceptSpaceAndBothEnterKeys() {
        OnlineButton button(QStringLiteral("加入房间"));
        QSignalSpy clicks(&button, &QPushButton::clicked);
        QTest::keyClick(&button, Qt::Key_Space);
        QTest::keyClick(&button, Qt::Key_Return);
        QTest::keyClick(&button, Qt::Key_Enter);
        QCOMPARE(clicks.size(), 3);
    }
};

} // namespace fpdz

QTEST_MAIN(fpdz::OnlineLobbyTest)
#include "test_online_lobby.moc"
