#include "online_window.h"

#include "ui/models/hand_list_model.h"
#include "ui/widgets/card_table_widget.h"

#include <QApplication>
#include <QJsonArray>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QTest>

namespace fpdz {

class OnlineWindowPlayTest final : public QObject {
    Q_OBJECT

private slots:
    void usesSinglePlayerHandControlsAndTable() {
        OnlineWindow window(nullptr, false);
        const QJsonArray seats{
            QJsonObject{{QStringLiteral("index"), 0}, {QStringLiteral("nickname"), QStringLiteral("甲")},
                        {QStringLiteral("remaining"), 17}},
            QJsonObject{{QStringLiteral("index"), 1}, {QStringLiteral("nickname"), QStringLiteral("我")},
                        {QStringLiteral("remaining"), 5}},
            QJsonObject{{QStringLiteral("index"), 2}, {QStringLiteral("nickname"), QStringLiteral("机器人")},
                        {QStringLiteral("remaining"), 16}}};
        QJsonObject view{{QStringLiteral("room_id"), QStringLiteral("3-01")},
            {QStringLiteral("round_id"), QStringLiteral("test-round")},
            {QStringLiteral("seq"), QStringLiteral("1")},
            {QStringLiteral("status"), QStringLiteral("playing")},
            {QStringLiteral("player_count"), 3},
            {QStringLiteral("self_seat"), 1},
            {QStringLiteral("phase"), 4},
            {QStringLiteral("current_player"), 1},
            {QStringLiteral("seats"), seats},
            {QStringLiteral("hand"), QJsonArray{0, 1, 4, 8, 9}},
            {QStringLiteral("bottom_revealed"), false}};
        window.refreshRoom(view);
        window.showRoom();

        QCOMPARE(window.handModel_->rowCount(), 5);
        QCOMPARE(window.cardTable_->displayedHumanCardCount(), 5);
        QCOMPARE(window.cardTable_->displayedOpponentBackCount(PlayerId::Player2), 16);
        QCOMPARE(window.cardTable_->displayedOpponentBackCount(PlayerId::Player3), 17);
        QCOMPARE(window.handView_->currentIndex().row(), 0);

        QTest::keyClick(window.handView_, Qt::Key_Right);
        QCOMPARE(window.handView_->currentIndex().row(), 2);
        QTest::keyClick(window.handView_, Qt::Key_Up);
        QCOMPARE(window.handModel_->selectedCount(), 1);
        QCOMPARE(window.cardTable_->displayedSelectedCardCount(), 1);
        QTest::keyClick(window.handView_, Qt::Key_Left);
        QCOMPARE(window.handView_->currentIndex().row(), 0);
        QTest::keyClick(window.handView_, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(window.handModel_->selectedCount(), 3);
        QTest::keyClick(window.handView_, Qt::Key_Down);
        QCOMPARE(window.handModel_->selectedCount(), 2);
        QTest::keyClick(window.handView_, Qt::Key_Down, Qt::ControlModifier);
        QCOMPARE(window.handModel_->selectedCount(), 0);
        QTest::keyClick(window.handView_, Qt::Key_Space);
        QCOMPARE(window.handModel_->selectedCount(), 0);
        QTest::keyClick(window.handView_, Qt::Key_End);
        QCOMPARE(window.handView_->currentIndex().row(), 3);
        QTest::keyClick(window.handView_, Qt::Key_Home);
        QCOMPARE(window.handView_->currentIndex().row(), 0);
        QTest::keyClick(window.handView_, Qt::Key_Right, Qt::ControlModifier);
        QCOMPARE(window.handView_->currentIndex().row(), 3);
        QTest::keyClick(window.handView_, Qt::Key_Left, Qt::ControlModifier);
        QCOMPARE(window.handView_->currentIndex().row(), 0);
        QPushButton* diagnostics = nullptr;
        for (auto* button : window.findChildren<QPushButton*>()) {
            if (button->text() == QStringLiteral("复制诊断信息")) diagnostics = button;
        }
        QVERIFY(diagnostics);
        QTest::keyClick(diagnostics, Qt::Key_Right);
        QCOMPARE(window.handView_->currentIndex().row(), 2);
        QTest::keyClick(diagnostics, Qt::Key_Home);
        QCOMPARE(window.handView_->currentIndex().row(), 0);
        QTest::keyClick(window.handView_, Qt::Key_H, Qt::AltModifier);
        QVERIFY(window.handModel_->selectedCount() > 0);
        QTest::keyClick(window.handView_, Qt::Key_Down, Qt::ControlModifier);
        QCOMPARE(window.handModel_->selectedCount(), 0);

        QTest::keyClick(window.handView_, Qt::Key_Up);
        QCOMPARE(window.handModel_->selectedCount(), 1);
        QTest::keyClick(window.handView_, Qt::Key_Return);
        QCOMPARE(window.announcementLabel_->text(), QStringLiteral("服务器未连接"));
        QTest::keyClick(window.handView_, Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(window.announcementLabel_->text(), QStringLiteral("服务器未连接"));
        view[QStringLiteral("seq")] = QStringLiteral("2");
        view[QStringLiteral("current_player")] = 2;
        window.refreshRoom(view);
        QCOMPARE(window.handModel_->selectedCount(), 1);
        view[QStringLiteral("seq")] = QStringLiteral("3");
        view[QStringLiteral("hand")] = QJsonArray{0, 4};
        window.refreshRoom(view);
        QCOMPARE(window.handModel_->selectedCount(), 0);
        QCOMPARE(window.cardTable_->displayedHumanCardCount(), 2);
    }
};

} // namespace fpdz

QTEST_MAIN(fpdz::OnlineWindowPlayTest)
#include "test_online_play_controls.moc"
