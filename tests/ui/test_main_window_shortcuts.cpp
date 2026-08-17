#include <QtTest>
#include <QtTest/QTestAccessibility>
#include <QMenu>
#include <QComboBox>
#include <QLineEdit>
#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QKeyEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListView>
#include <QListWidget>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QStatusBar>
#include <QSpinBox>
#include <QTimer>
#include <QAction>
#include <QPushButton>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "accessibility/accessibility_service.h"
#include "core/engine/game_engine.h"
#include "core/model/card.h"
#include "core/rules/pattern_analyzer.h"
#include "ui/main_window.h"
#include "ui/dialogs/settings_dialog.h"
#include "ui/models/hand_list_model.h"
#include "ui/sound_service.h"
#include "persistence/diagnostic_trace_service.h"
#include "persistence/data_paths.h"

using namespace fpdz;

class TestMainWindowShortcuts : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        qApp->setProperty("fpdz.suppressStartupPrompts", true);
        qApp->setProperty("fpdz.suppressExternalHelp", true);
        qApp->setProperty("fpdz.suppressKeyboardHook", true);
        QDir(DataPaths::appDataDir()).removeRecursively();
    }

    void testSettingsExposeExactlyThreeLocalRobotModes() {
        AppSettings settings;
        settings.aiDifficulty = static_cast<int>(AiDifficulty::Advanced);
        SettingsDialog dialog(settings);

        auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("aiModeComboBox"));
        QVERIFY(combo);
        QCOMPARE(combo->count(), 3);
        QCOMPARE(combo->itemText(0), QString::fromUtf8(u8"初级"));
        QCOMPARE(combo->itemText(1), QString::fromUtf8(u8"中级"));
        QCOMPARE(combo->itemText(2), QString::fromUtf8(u8"高级"));
        QCOMPARE(combo->currentData().toInt(), static_cast<int>(AiDifficulty::Advanced));
        QCOMPARE(dialog.settings().aiDifficulty, static_cast<int>(AiDifficulty::Advanced));
        QVERIFY(!dialog.findChild<QComboBox*>(QStringLiteral("remoteAiPolicyModeComboBox")));
        auto* voiceCombo = dialog.findChild<QComboBox*>(QStringLiteral("humanVoiceComboBox"));
        auto* musicMode = dialog.findChild<QComboBox*>(QStringLiteral("backgroundMusicModeComboBox"));
        QVERIFY(voiceCombo);
        QVERIFY(musicMode);
        auto* updateChecks = dialog.findChild<QCheckBox*>(
            QStringLiteral("automaticUpdateChecksCheckBox"));
        QVERIFY(updateChecks);
        QVERIFY(updateChecks->isChecked());
        QCOMPARE(voiceCombo->currentText(), QString::fromUtf8(u8"男声"));
        QCOMPARE(musicMode->currentText(), QString::fromUtf8(u8"自动切换"));
    }

    void testAnnouncementsUseTraditionalFocusEventWithoutMovingKeyboardFocus() {
        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.setFocus();
        QTRY_VERIFY(window.hasFocus());

        auto* statusLabel = window.statusBar()->findChild<QLabel*>();
        QVERIFY(statusLabel);
        QCOMPARE(statusLabel->objectName(), QStringLiteral("gameStatusAnnouncement"));
        QVERIFY(statusLabel->accessibleDescription().isEmpty());
        QAccessibleInterface* statusInterface = QAccessible::queryAccessibleInterface(statusLabel);
        QVERIFY(statusInterface);
        QCOMPARE(statusInterface->role(), QAccessible::MenuItem);
        QVERIFY(statusInterface->text(QAccessible::Description).isEmpty());
        QVERIFY(statusInterface->text(QAccessible::Help).isEmpty());
        QVERIFY(statusInterface->actionInterface() == nullptr);
        QWidget* focusBefore = QApplication::focusWidget();
        QCOMPARE(focusBefore, &window);

        QTestAccessibility::initialize();
        QTestAccessibility::clearEvents();

        Announcement announcement;
        announcement.text = L"传统事件测试";
        announcement.category = AnnouncementCategory::System;
        QVERIFY(accessibility.announce(announcement, statusLabel));

        const auto events = QTestAccessibility::events();
        int focusEventCount = 0;
        int announcementEventCount = 0;
        for (const QAccessibleEvent* event : events) {
            if (event->type() == QAccessible::Focus && event->object() == statusLabel) {
                ++focusEventCount;
            }
            if (event->type() == QAccessible::Announcement) {
                ++announcementEventCount;
            }
        }
        QCOMPARE(focusEventCount, 1);
        QCOMPARE(announcementEventCount, 0);
        QCOMPARE(statusLabel->accessibleName(), QString::fromUtf8(u8"传统事件测试"));
        QCOMPARE(statusInterface->text(QAccessible::Name),
                 QString::fromUtf8(u8"传统事件测试"));
        QVERIFY(statusInterface->text(QAccessible::Description).isEmpty());
        QCOMPARE(QApplication::focusWidget(), focusBefore);

        QTestAccessibility::clearEvents();
        QVERIFY(!accessibility.announce(announcement, statusLabel));
        QVERIFY(QTestAccessibility::events().isEmpty());
        QTestAccessibility::cleanup();
    }

    void testLegacyRemoteAndAccessibilitySettingsAreIgnored() {
        QJsonObject legacy;
        legacy.insert(QStringLiteral("aiDifficulty"), 3);
        legacy.insert(QStringLiteral("remoteAiEnabled"), true);
        legacy.insert(QStringLiteral("accessibilityBackend"), 3);
        legacy.insert(QStringLiteral("screenReaderCompatibilityMode"), true);
        const AppSettings migrated = AppSettings::fromJson(legacy);
        QCOMPARE(migrated.aiDifficulty, static_cast<int>(AiDifficulty::Advanced));
        const QJsonObject saved = migrated.toJson();
        QVERIFY(!saved.contains(QStringLiteral("remoteAiEnabled")));
        QVERIFY(!saved.contains(QStringLiteral("accessibilityBackend")));
        QVERIFY(!saved.contains(QStringLiteral("screenReaderCompatibilityMode")));
    }

    void testAutoPassSecondsSupportsAcceleratedHold() {
        AppSettings settings;
        SettingsDialog dialog(settings);
        auto* spinBox = dialog.findChild<QSpinBox*>(QStringLiteral("autoPassSecondsSpinBox"));
        QVERIFY(spinBox);
        QCOMPARE(spinBox->minimum(), 3);
        QCOMPARE(spinBox->maximum(), 1800);
        QCOMPARE(spinBox->singleStep(), 1);
        QVERIFY(spinBox->isAccelerated());
    }

    void testSettingsAudioValuesApplyOnlyAfterAccepted() {
        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        auto* sound = window.findChild<SoundService*>();
        QVERIFY(sound);
        const int originalEffectsVolume = sound->volume();
        const int originalMusicVolume = sound->musicVolume();
        const int pendingEffectsVolume = originalEffectsVolume == 26 ? 27 : 26;
        const int pendingMusicVolume = originalMusicVolume == 48 ? 49 : 48;
        bool pendingValuesStayedInactive = false;
        bool sawSettingsDialog = false;

        QTimer closeSettingsDialog;
        closeSettingsDialog.setInterval(20);
        connect(&closeSettingsDialog, &QTimer::timeout, this, [&]() {
            auto* dialog = qobject_cast<SettingsDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            sawSettingsDialog = true;
            auto* effects = dialog->findChild<QSpinBox*>(
                QStringLiteral("soundVolumeSpinBox"));
            auto* music = dialog->findChild<QSpinBox*>(
                QStringLiteral("backgroundMusicVolumeSpinBox"));
            if (!effects || !music) {
                dialog->reject();
                closeSettingsDialog.stop();
                return;
            }
            effects->setValue(pendingEffectsVolume);
            music->setValue(pendingMusicVolume);
            pendingValuesStayedInactive = sound->volume() == originalEffectsVolume &&
                sound->musicVolume() == originalMusicVolume;
            dialog->accept();
            closeSettingsDialog.stop();
        });
        closeSettingsDialog.start();

        QTest::keyClick(&window, Qt::Key_F5);
        QVERIFY(sawSettingsDialog);
        QVERIFY(pendingValuesStayedInactive);
        QCOMPARE(sound->volume(), pendingEffectsVolume);
        QCOMPARE(sound->musicVolume(), pendingMusicVolume);
    }

    void testHelpActionsOpenRealTxtFiles() {
        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);

        const std::array<std::pair<QString, QString>, 4> cases = {{
            {QStringLiteral("shortcutHelpAction"),
             QString::fromUtf8(u8"飞船单机斗地主快捷键说明.txt")},
            {QStringLiteral("rulesHelpAction"),
             QString::fromUtf8(u8"飞船单机斗地主玩法说明.txt")},
            {QStringLiteral("userGuideAction"),
             QString::fromUtf8(u8"飞船单机斗地主详细使用说明.txt")},
            {QStringLiteral("changelogAction"),
             QString::fromUtf8(u8"飞船单机斗地主更新日志.txt")}
        }};
        for (const auto& [objectName, fileName] : cases) {
            auto* action = window.findChild<QAction*>(objectName);
            QVERIFY(action);
            qApp->setProperty("fpdz.lastHelpFilePath", QVariant());
            action->trigger();
            const QFileInfo opened(qApp->property("fpdz.lastHelpFilePath").toString());
            QCOMPARE(opened.fileName(), fileName);
            QVERIFY(opened.exists());
            QVERIFY(opened.size() > 0);
        }
    }

    void testHandViewCanReceiveScreenReaderFocus() {
        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        auto* handView = window.findChild<QListView*>();
        QVERIFY(handView);
        QCOMPARE(handView->focusPolicy(), Qt::StrongFocus);
        QCOMPARE(handView->selectionMode(), QAbstractItemView::NoSelection);
    }

    void testCopyDiagnosticInformationUsesSanitizedClipboardReport() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        DiagnosticTraceService trace;
        trace.init(temp.path());
        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility, &trace);
        auto* action = window.findChild<QAction*>(QStringLiteral("copyDiagnosticAction"));
        QVERIFY(action);
        QApplication::clipboard()->clear();
        action->trigger();
        const QString report = QApplication::clipboard()->text();
        QVERIFY(report.contains(QString::fromUtf8(u8"飞船单机斗地主诊断报告")));
        QVERIFY(report.contains(QStringLiteral("读屏状态")));
        QVERIFY(report.contains(QStringLiteral("当前朗读路线")));
        QVERIFY(!report.contains(QStringLiteral("screen_reader_compatibility_mode")));
        QVERIFY(!report.contains(QStringLiteral("remote_ai_enabled")));
        QVERIFY(report.contains(QStringLiteral("焦点与手牌")));
        QVERIFY(!report.contains(QStringLiteral("api_key"), Qt::CaseInsensitive));
        QVERIFY(!report.contains(QStringLiteral("authorization"), Qt::CaseInsensitive));
        QVERIFY(!report.contains(QDir::homePath(), Qt::CaseInsensitive));
        QVERIFY(report.size() <= 1024 * 1024);
    }

    void testSingleCardSelectionSpeaksOnlyThePickedRank() {
        HandListModel model;
        const std::vector<Card> cards = {
            Card::create(Rank::Ten, Suit::Spades, 0),
            Card::create(Rank::Ten, Suit::Hearts, 0),
            Card::create(Rank::Ten, Suit::Clubs, 0)
        };
        QVERIFY(model.setCards(cards));
        QCOMPARE(model.data(model.index(0, 0), Qt::DisplayRole).toString(),
                 QString::fromUtf8(u8"3张10"));

        for (int row = 0; row < 3; ++row) {
            QVERIFY(model.selectSingle(row));
            QCOMPARE(model.data(model.index(row, 0), Qt::AccessibleTextRole).toString(),
                     QStringLiteral("10"));
            QCOMPARE(model.selectedCount(), row + 1);
            QCOMPARE(model.nextUnselectedRow(row), row < 2 ? row + 1 : -1);
        }
    }

    void testGroupSelectionIsExactAndCumulative() {
        HandListModel model;
        const std::vector<Card> cards = {
            Card::create(Rank::Three, Suit::Spades, 0),
            Card::create(Rank::Three, Suit::Hearts, 0),
            Card::create(Rank::Three, Suit::Clubs, 0),
            Card::create(Rank::Four, Suit::Spades, 0),
            Card::create(Rank::Four, Suit::Hearts, 0),
            Card::create(Rank::Four, Suit::Clubs, 0),
            Card::create(Rank::Four, Suit::Diamonds, 0),
            Card::create(Rank::Five, Suit::Spades, 0),
            Card::create(Rank::Five, Suit::Hearts, 0),
            Card::create(Rank::Five, Suit::Clubs, 0),
            Card::create(Rank::Five, Suit::Diamonds, 0),
            Card::create(Rank::Five, Suit::Spades, 1),
            Card::create(Rank::Six, Suit::Spades, 0),
            Card::create(Rank::Six, Suit::Hearts, 0)
        };
        QVERIFY(model.setCards(cards));

        const auto threes = model.selectGroup(0);
        QCOMPARE(threes.groupCount, 3);
        QCOMPARE(threes.newlySelectedCount, 3);
        QCOMPARE(model.selectedCount(), 3);

        const auto repeated = model.selectGroup(1);
        QCOMPARE(repeated.groupCount, 3);
        QCOMPARE(repeated.newlySelectedCount, 0);
        QCOMPARE(model.selectedCount(), 3);

        const auto fours = model.selectGroup(3);
        QCOMPARE(fours.groupCount, 4);
        QCOMPARE(model.selectedCount(), 7);

        const auto fives = model.selectGroup(7);
        QCOMPARE(fives.groupCount, 5);
        QCOMPARE(model.selectedCount(), 12);
        QVERIFY(!model.setCards(cards));
        QCOMPARE(model.selectedCount(), 12);
    }

    void testUnsortedBottomCardsStillFormCompleteRankGroups() {
        HandListModel model;
        const std::vector<Card> cards = {
            Card::create(Rank::Four, Suit::Spades, 0),
            Card::create(Rank::Four, Suit::Hearts, 0),
            Card::create(Rank::Five, Suit::Spades, 0),
            Card::create(Rank::Seven, Suit::Spades, 0),
            Card::create(Rank::Seven, Suit::Hearts, 0),
            Card::create(Rank::Eight, Suit::Spades, 0),
            Card::create(Rank::Four, Suit::Clubs, 1),
            Card::create(Rank::Eight, Suit::Hearts, 1),
            Card::create(Rank::Seven, Suit::Clubs, 1),
            Card::create(Rank::Seven, Suit::Diamonds, 1)
        };
        QVERIFY(model.setCards(cards));

        QCOMPARE(model.cardAt(0).rank(), Rank::Four);
        QCOMPARE(model.cardAt(1).rank(), Rank::Four);
        QCOMPARE(model.cardAt(2).rank(), Rank::Four);
        const auto fours = model.selectGroup(0);
        QCOMPARE(fours.groupCount, 3);
        QCOMPARE(fours.newlySelectedCount, 3);
        QCOMPARE(model.selectedCount(), 3);

        const int sevenRow = model.groupStartRow(4);
        QCOMPARE(model.cardAt(sevenRow).rank(), Rank::Seven);
        const auto sevens = model.selectGroup(sevenRow);
        QCOMPARE(sevens.groupCount, 4);
        QCOMPARE(sevens.newlySelectedCount, 4);
        QCOMPARE(model.selectedCount(), 7);

        const int eightRow = model.nextGroupStartRow(sevenRow);
        const auto eights = model.selectGroup(eightRow);
        QCOMPARE(eights.groupCount, 2);
        QCOMPARE(model.selectedCount(), 9);
    }

    void testTripleWithPairCanBeSelectedAsTwoGroups() {
        HandListModel model;
        const std::vector<Card> cards = {
            Card::create(Rank::Three, Suit::Spades, 0),
            Card::create(Rank::Three, Suit::Hearts, 0),
            Card::create(Rank::Three, Suit::Clubs, 0),
            Card::create(Rank::Six, Suit::Spades, 0),
            Card::create(Rank::Six, Suit::Hearts, 0)
        };
        QVERIFY(model.setCards(cards));
        QCOMPARE(model.selectGroup(0).newlySelectedCount, 3);
        QCOMPARE(model.selectGroup(3).newlySelectedCount, 2);
        QCOMPARE(model.selectedCount(), 5);

        const auto pattern = PatternAnalyzer::analyze(model.selectedCards());
        QVERIFY(pattern.isValid());
        QCOMPARE(pattern.type, CardPatternType::TripleWithPair);
        QCOMPARE(pattern.mainRank, Rank::Three);
    }

    void testCardSelectionKeyboardCommandsAreExactAndCompatible() {
        GameEngine engine;
        auto& state = engine.state();
        auto& fullState = state.fullState();
        state.setPhase(GamePhase::Playing);
        fullState.currentPlayer = PlayerId::Player1;
        fullState.players[0].hand.clear();
        fullState.players[0].hand.addCards({
            Card::create(Rank::Three, Suit::Spades, 0),
            Card::create(Rank::Three, Suit::Hearts, 0),
            Card::create(Rank::Three, Suit::Clubs, 0),
            Card::create(Rank::Four, Suit::Spades, 0),
            Card::create(Rank::Four, Suit::Hearts, 0),
            Card::create(Rank::Four, Suit::Clubs, 0),
            Card::create(Rank::Seven, Suit::Spades, 0),
            Card::create(Rank::Seven, Suit::Hearts, 0),
            Card::create(Rank::Seven, Suit::Clubs, 0),
            Card::create(Rank::Seven, Suit::Diamonds, 0)
        });
        fullState.players[0].hand.sortByRank();

        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.refreshFromState();

        auto* handView = window.findChild<QListView*>();
        QVERIFY(handView);
        auto* handModel = qobject_cast<HandListModel*>(handView->model());
        QVERIFY(handModel);

        QTest::keyClick(&window, Qt::Key_Home);
        QCOMPARE(handView->currentIndex().row(), 0);
        QTest::keyClick(&window, Qt::Key_Right);
        QCOMPARE(handView->currentIndex().row(), 3);
        QTest::keyClick(&window, Qt::Key_Right);
        QCOMPARE(handView->currentIndex().row(), 6);

        QTest::keyClick(&window, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 4);
        for (int row = 6; row < 10; ++row) QVERIFY(handModel->isSelected(row));

        QTest::keyClick(&window, Qt::Key_Down, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 0);

        QTest::keyClick(&window, Qt::Key_Home);
        auto* statusLabel = window.statusBar()->findChild<QLabel*>();
        QVERIFY(statusLabel);
        const QString statusBeforeSilentPick = statusLabel->text();
        QTest::keyClick(&window, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 3);
        QCOMPARE(handView->currentIndex().row(), 0);
        QTest::keyClick(&window, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 6);
        QCOMPARE(handView->currentIndex().row(), 3);

        QTest::keyClick(&window, Qt::Key_Down, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 0);
        QTest::keyClick(&window, Qt::Key_Up);
        QCOMPARE(handModel->selectedCount(), 1);
        QVERIFY(handModel->isSelected(3));
        QCOMPARE(handView->currentIndex().row(), 3);
        QCOMPARE(statusLabel->text(), statusBeforeSilentPick);
        QTest::keyClick(&window, Qt::Key_Up);
        QCOMPARE(handModel->selectedCount(), 2);
        QVERIFY(handModel->isSelected(4));
        QCOMPARE(handView->currentIndex().row(), 4);
        QCOMPARE(statusLabel->text(), statusBeforeSilentPick);
        QTest::keyClick(&window, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 3);
        QCOMPARE(handView->currentIndex().row(), 3);
        QVERIFY(handModel->isSelected(5));

        QTest::keyClick(&window, Qt::Key_Down);
        QCOMPARE(handModel->selectedCount(), 2);
        QVERIFY(!handModel->isSelected(3));
        QVERIFY(handModel->isSelected(4));
        QVERIFY(handModel->isSelected(5));
        QTest::keyClick(&window, Qt::Key_Down, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 0);

        QTest::keyClick(&window, Qt::Key_Home);
        QTest::keyClick(&window, Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(handView->currentIndex().row(), 1);
        QTest::keyClick(&window, Qt::Key_Left, Qt::ShiftModifier);
        QCOMPARE(handView->currentIndex().row(), 0);
        QTest::keyClick(&window, Qt::Key_Right);
        QCOMPARE(handView->currentIndex().row(), 3);

        fullState.currentPlayer = PlayerId::Player2;
        window.refreshFromState();
        QTest::keyClick(&window, Qt::Key_Home);
        QTest::keyClick(&window, Qt::Key_Up);
        QCOMPARE(handModel->selectedCount(), 1);
        window.refreshFromState();
        QCOMPARE(handModel->selectedCount(), 1);
        QTest::keyClick(&window, Qt::Key_Down);
        QCOMPARE(handModel->selectedCount(), 0);
        QTest::keyClick(&window, Qt::Key_Up, Qt::ControlModifier);
        QVERIFY(handModel->selectedCount() >= 1);
        const int selectedWhileWaiting = handModel->selectedCount();
        const int handSizeWhileWaiting = fullState.players[0].hand.size();
        window.refreshFromState();
        QCOMPARE(handModel->selectedCount(), selectedWhileWaiting);
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(fullState.players[0].hand.size(), handSizeWhileWaiting);
        QCOMPARE(handModel->selectedCount(), selectedWhileWaiting);
        QTest::keyClick(&window, Qt::Key_Down, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 0);

        state.setPhase(GamePhase::NotStarted);
        window.close();
    }

    void testUpArrowSelectsSequentialCardsAcrossAdjacentRanks() {
        GameEngine engine;
        auto& state = engine.state();
        auto& fullState = state.fullState();
        state.setPhase(GamePhase::Playing);
        fullState.currentPlayer = PlayerId::Player1;
        fullState.players[0].role = Role::Landlord;
        for (size_t index = 1; index < fullState.players.size(); ++index) {
            fullState.players[index].role = Role::Farmer;
        }
        fullState.players[0].hand.addCards({
            Card::create(Rank::Nine, Suit::Spades, 0),
            Card::create(Rank::Nine, Suit::Hearts, 0),
            Card::create(Rank::Nine, Suit::Clubs, 0),
            Card::create(Rank::Ten, Suit::Spades, 0),
            Card::create(Rank::Ten, Suit::Hearts, 0),
            Card::create(Rank::Ten, Suit::Clubs, 0)
        });
        fullState.players[0].hand.sortByRank();

        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.refreshFromState();

        auto* handView = window.findChild<QListView*>();
        QVERIFY(handView);
        auto* handModel = qobject_cast<HandListModel*>(handView->model());
        QVERIFY(handModel);

        QTest::keyClick(&window, Qt::Key_Home);
        for (int selected = 1; selected <= 6; ++selected) {
            QTest::keyClick(&window, Qt::Key_Up);
            QCOMPARE(handModel->selectedCount(), selected);
            QCOMPARE(handView->currentIndex().row(), selected - 1);
        }

        QTest::keyClick(&window, Qt::Key_Left, Qt::ShiftModifier);
        QTest::keyClick(&window, Qt::Key_Left, Qt::ShiftModifier);
        QTest::keyClick(&window, Qt::Key_Left, Qt::ShiftModifier);
        QCOMPARE(handView->currentIndex().row(), 5);
        QTest::keyClick(&window, Qt::Key_Down);
        QCOMPARE(handModel->selectedCount(), 5);
        QVERIFY(!handModel->isSelected(5));
        for (int row : {0, 1, 2, 3, 4}) QVERIFY(handModel->isSelected(row));
        QTest::keyClick(&window, Qt::Key_Up);
        QCOMPARE(handModel->selectedCount(), 6);
        QVERIFY(handModel->isSelected(2));

        const auto pattern = PatternAnalyzer::analyze(handModel->selectedCards());
        QVERIFY(pattern.isValid());
        QCOMPARE(pattern.type, CardPatternType::Airplane);
        QCOMPARE(pattern.totalCards, 6);

        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(fullState.players[0].hand.size(), 0);
        window.close();
    }

    void testHoldingControlSelectsAdjacentGroupsOnRepeatedUpArrow() {
        GameEngine engine;
        auto& state = engine.state();
        auto& fullState = state.fullState();
        state.setPhase(GamePhase::Playing);
        fullState.currentPlayer = PlayerId::Player1;
        fullState.players[0].hand.addCards({
            Card::create(Rank::Three, Suit::Spades, 0),
            Card::create(Rank::Three, Suit::Hearts, 0),
            Card::create(Rank::Three, Suit::Clubs, 0),
            Card::create(Rank::Three, Suit::Diamonds, 0),
            Card::create(Rank::Four, Suit::Spades, 0),
            Card::create(Rank::Four, Suit::Hearts, 0),
            Card::create(Rank::Four, Suit::Clubs, 0),
            Card::create(Rank::Four, Suit::Diamonds, 0)
        });
        fullState.players[0].hand.sortByRank();

        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.refreshFromState();

        auto* handView = window.findChild<QListView*>();
        QVERIFY(handView);
        auto* handModel = qobject_cast<HandListModel*>(handView->model());
        QVERIFY(handModel);

        QKeyEvent controlPress(QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier);
        QApplication::sendEvent(&window, &controlPress);
        for (int press = 0; press < 2; ++press) {
            QKeyEvent upPress(QEvent::KeyPress, Qt::Key_Up, Qt::ControlModifier);
            QApplication::sendEvent(&window, &upPress);
            QKeyEvent upRelease(QEvent::KeyRelease, Qt::Key_Up, Qt::ControlModifier);
            QApplication::sendEvent(&window, &upRelease);
        }
        QKeyEvent controlRelease(QEvent::KeyRelease, Qt::Key_Control, Qt::NoModifier);
        QApplication::sendEvent(&window, &controlRelease);

        QCOMPARE(handModel->selectedCount(), 8);
        QCOMPARE(handView->currentIndex().row(), 4);
        for (int row = 0; row < 8; ++row) QVERIFY(handModel->isSelected(row));

        state.setPhase(GamePhase::NotStarted);
        window.close();
    }

    void testSelectedTripleStaysPickedWhenAddingPairWithControlUp() {
        GameEngine engine;
        auto& state = engine.state();
        auto& fullState = state.fullState();
        state.setPhase(GamePhase::Playing);
        fullState.currentPlayer = PlayerId::Player1;
        fullState.players[0].role = Role::Landlord;
        for (size_t index = 1; index < fullState.players.size(); ++index) {
            fullState.players[index].role = Role::Farmer;
        }
        fullState.players[0].hand.addCards({
            Card::create(Rank::Four, Suit::Spades, 0),
            Card::create(Rank::Four, Suit::Hearts, 0),
            Card::create(Rank::Four, Suit::Clubs, 0),
            Card::create(Rank::Eight, Suit::Spades, 0),
            Card::create(Rank::Eight, Suit::Hearts, 0)
        });
        fullState.players[0].hand.sortByRank();

        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.refreshFromState();

        auto* handView = window.findChild<QListView*>();
        QVERIFY(handView);
        auto* handModel = qobject_cast<HandListModel*>(handView->model());
        QVERIFY(handModel);

        QTest::keyClick(&window, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 3);
        QTest::keyClick(&window, Qt::Key_Right);
        QCOMPARE(handView->currentIndex().row(), 3);
        QTest::keyClick(&window, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 5);

        const auto pattern = PatternAnalyzer::analyze(handModel->selectedCards());
        QVERIFY(pattern.isValid());
        QCOMPARE(pattern.type, CardPatternType::TripleWithPair);
        QCOMPARE(pattern.mainRank, Rank::Four);
        QCOMPARE(pattern.totalCards, 5);

        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(fullState.players[0].hand.size(), 0);
        window.close();
    }

    void testSelectedQueensStayPickedWhenAddingTwoCardsIndividually() {
        GameEngine engine;
        auto& state = engine.state();
        auto& fullState = state.fullState();
        state.setPhase(GamePhase::Playing);
        fullState.currentPlayer = PlayerId::Player1;
        fullState.players[0].role = Role::Landlord;
        for (size_t index = 1; index < fullState.players.size(); ++index) {
            fullState.players[index].role = Role::Farmer;
        }
        fullState.players[0].hand.addCards({
            Card::create(Rank::Four, Suit::Spades, 0),
            Card::create(Rank::Four, Suit::Hearts, 0),
            Card::create(Rank::Queen, Suit::Spades, 0),
            Card::create(Rank::Queen, Suit::Hearts, 0),
            Card::create(Rank::Queen, Suit::Clubs, 0)
        });
        fullState.players[0].hand.sortByRank();

        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.refreshFromState();

        auto* handView = window.findChild<QListView*>();
        QVERIFY(handView);
        auto* handModel = qobject_cast<HandListModel*>(handView->model());
        QVERIFY(handModel);

        QTest::keyClick(&window, Qt::Key_End);
        QCOMPARE(handView->currentIndex().row(), 2);
        QTest::keyClick(&window, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 3);

        QTest::keyClick(&window, Qt::Key_Home);
        QTest::keyClick(&window, Qt::Key_Up);
        QCOMPARE(handModel->selectedCount(), 4);
        QCOMPARE(handView->currentIndex().row(), 0);
        QTest::keyClick(&window, Qt::Key_Up);
        QCOMPARE(handModel->selectedCount(), 5);
        QVERIFY(handModel->isSelected(0));
        QVERIFY(handModel->isSelected(1));

        const auto pattern = PatternAnalyzer::analyze(handModel->selectedCards());
        QVERIFY(pattern.isValid());
        QCOMPARE(pattern.type, CardPatternType::TripleWithPair);
        QCOMPARE(pattern.mainRank, Rank::Queen);

        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(fullState.players[0].hand.size(), 0);
        window.close();
    }

    void testDiagnosticTraceRecordsKeysAndSelectionChanges() {
        QTemporaryDir traceDir;
        QVERIFY(traceDir.isValid());
        DiagnosticTraceService trace;
        trace.init(traceDir.path());

        GameEngine engine;
        engine.state().setPhase(GamePhase::Playing);
        auto& fullState = engine.state().fullState();
        fullState.currentPlayer = PlayerId::Player1;
        fullState.players[0].hand.addCards({
            Card::create(Rank::Seven, Suit::Spades, 0),
            Card::create(Rank::Seven, Suit::Hearts, 0),
            Card::create(Rank::Eight, Suit::Spades, 0),
            Card::create(Rank::Seven, Suit::Clubs, 1),
            Card::create(Rank::Seven, Suit::Diamonds, 1)
        });

        AccessibilityService accessibility;
        MainWindow window(engine, accessibility, &trace);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.refreshFromState();

        QTest::keyClick(&window, Qt::Key_Up, Qt::ControlModifier);
        trace.flush();

        QFile file(trace.traceFilePath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        bool sawControlUp = false;
        bool sawFourSevens = false;
        while (!file.atEnd()) {
            const auto document = QJsonDocument::fromJson(file.readLine());
            if (!document.isObject()) continue;
            const auto event = document.object();
            if (event.value(QStringLiteral("type")) == QStringLiteral("keyboard") &&
                event.value(QStringLiteral("key_name")) == QStringLiteral("Up") &&
                event.value(QStringLiteral("ctrl")).toBool()) {
                sawControlUp = true;
            }
            if (event.value(QStringLiteral("type")) == QStringLiteral("hand_action") &&
                event.value(QStringLiteral("action")) == QStringLiteral("take_group") &&
                event.value(QStringLiteral("group_count")).toInt() == 4 &&
                event.value(QStringLiteral("newly_selected_count")).toInt() == 4) {
                sawFourSevens = true;
            }
        }
        QVERIFY(sawControlUp);
        QVERIFY(sawFourSevens);

        engine.state().setPhase(GamePhase::NotStarted);
        window.close();
    }

    void testAltF4ClosesApplicationFromModalInterface() {
        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        QDialog dialog(&window);
        dialog.setModal(true);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));
        QTest::keyClick(&dialog, Qt::Key_F4, Qt::AltModifier);
        QTRY_VERIFY(!dialog.isVisible());
        QTRY_VERIFY(!window.isVisible());
    }

    void testOnlyThreeTopLevelMenusAndAltXStillCloses() {
        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        QVERIFY(!window.findChild<QAction*>(QStringLiteral("topLevelQuitAction")));
        QCOMPARE(window.menuBar()->actions().size(), 3);
        QTest::keyClick(&window, Qt::Key_X, Qt::AltModifier);
        QTRY_VERIFY(!window.isVisible());
    }

    void testAltThenTabCyclesThreeMenusAndAltMenuShortcutsAreReleased() {
        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QTRY_VERIFY(!qobject_cast<QMenuBar*>(QApplication::focusWidget()));

        window.menuBar()->setActiveAction(nullptr);
        QTest::keyClick(&window, Qt::Key_Tab);
        QCOMPARE(window.menuBar()->activeAction(), nullptr);
        QVERIFY(!window.menuBar()->hasFocus());

        auto* playerNamesMenu = window.findChild<QMenu*>(QStringLiteral("playerNamesMenu"));
        auto* gameMenu = window.findChild<QMenu*>(QStringLiteral("gameMenu"));
        auto* settingsMenu = window.findChild<QMenu*>(QStringLiteral("settingsMenu"));
        auto* soundManagerAction =
            window.findChild<QAction*>(QStringLiteral("soundManagerAction"));
        auto* gameMenuAction = window.findChild<QAction*>(QStringLiteral("gameMenuAction"));
        auto* playerNamesMenuAction =
            window.findChild<QAction*>(QStringLiteral("playerNamesMenuAction"));
        QVERIFY(gameMenu);
        QVERIFY(settingsMenu);
        QVERIFY(soundManagerAction);
        QVERIFY(gameMenuAction);
        QVERIFY(playerNamesMenu);
        QVERIFY(!window.findChild<QMenu*>(QStringLiteral("remoteAiPolicyMenu")));
        QVERIFY(!window.findChild<QAction*>(QStringLiteral("apiCredentialsAction")));
        QVERIFY(playerNamesMenuAction);
        QVERIFY(playerNamesMenu->title().contains(QString::fromUtf8(u8"玩家名称设置")));
        QVERIFY(playerNamesMenu->actions().size() >= 5);
        settingsMenu->menuAction()->trigger();
        QTRY_COMPARE(qobject_cast<QMenu*>(QApplication::activePopupWidget()), settingsMenu);
        playerNamesMenuAction->trigger();
        QTRY_COMPARE(qobject_cast<QMenu*>(QApplication::activePopupWidget()), playerNamesMenu);
        QTest::keyClick(playerNamesMenu, Qt::Key_Escape);
        QTest::keyClick(settingsMenu, Qt::Key_Escape);
        QTRY_VERIFY(!QApplication::activePopupWidget());

        window.menuBar()->setActiveAction(nullptr);
        window.setFocus(Qt::OtherFocusReason);
        QTest::keyPress(&window, Qt::Key_Alt);
        QTest::keyRelease(&window, Qt::Key_Alt);
        QTRY_VERIFY(window.menuBar()->hasFocus());
        QCOMPARE(window.menuBar()->activeAction()->text(), QString::fromUtf8(u8"游戏(G)"));
        QTest::keyClick(&window, Qt::Key_Tab);
        QCOMPARE(window.menuBar()->activeAction()->text(), QString::fromUtf8(u8"设置(S)"));
        QTest::keyClick(&window, Qt::Key_Tab);
        QCOMPARE(window.menuBar()->activeAction()->text(), QString::fromUtf8(u8"帮助(H)"));
        QTest::keyClick(&window, Qt::Key_Tab);
        QCOMPARE(window.menuBar()->activeAction()->text(), QString::fromUtf8(u8"游戏(G)"));
        QTest::keyClick(window.menuBar(), Qt::Key_Return);
        QTRY_VERIFY(QApplication::activePopupWidget());
        auto* gamePopup = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        QVERIFY(gamePopup);
        QCOMPARE(gamePopup, window.findChild<QMenu*>(QStringLiteral("gameMenu")));
        QTest::keyClick(gamePopup, Qt::Key_Escape);
        QTRY_VERIFY(!QApplication::activePopupWidget());

        for (Qt::Key key : {Qt::Key_G, Qt::Key_S, Qt::Key_H}) {
            QTest::keyClick(&window, key, Qt::AltModifier);
            QVERIFY(!QApplication::activePopupWidget());
        }
    }

    void testF1ClosesOpenMenuBeforeStartingBattle() {
        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        auto* settingsMenu = window.findChild<QMenu*>(QStringLiteral("settingsMenu"));
        QVERIFY(settingsMenu);

        settingsMenu->menuAction()->trigger();
        QTRY_COMPARE(qobject_cast<QMenu*>(QApplication::activePopupWidget()), settingsMenu);
        QTest::keyClick(settingsMenu, Qt::Key_F1);

        QTRY_COMPARE(engine.state().phase(), GamePhase::Bidding);
        QTRY_VERIFY(!QApplication::activePopupWidget());
        QCOMPARE(window.menuBar()->activeAction(), nullptr);
        QTRY_VERIFY(QApplication::focusWidget());
        QVERIFY(!qobject_cast<QMenuBar*>(QApplication::focusWidget()));
        QVERIFY(!qobject_cast<QMenu*>(QApplication::focusWidget()));
    }

    void testF1ClosesActiveMenuBarBeforeStartingBattle() {
        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        QTest::keyPress(&window, Qt::Key_Alt);
        QTest::keyRelease(&window, Qt::Key_Alt);
        QTRY_VERIFY(window.menuBar()->hasFocus());
        QTest::keyClick(window.menuBar(), Qt::Key_Tab);
        QCOMPARE(window.menuBar()->activeAction(), window.menuBar()->actions()[1]);

        QTest::keyClick(window.menuBar(), Qt::Key_F1);

        QTRY_COMPARE(engine.state().phase(), GamePhase::Bidding);
        QTRY_VERIFY(!QApplication::activePopupWidget());
        QCOMPARE(window.menuBar()->activeAction(), nullptr);
        QTRY_VERIFY(QApplication::focusWidget());
        QVERIFY(!qobject_cast<QMenuBar*>(QApplication::focusWidget()));
        QVERIFY(!qobject_cast<QMenu*>(QApplication::focusWidget()));
    }

    void testNativeF1ClosesOpenMenuBeforeStartingBattle() {
#ifdef Q_OS_WIN
        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        auto* settingsMenu = window.findChild<QMenu*>(QStringLiteral("settingsMenu"));
        QVERIFY(settingsMenu);

        settingsMenu->menuAction()->trigger();
        QTRY_COMPARE(qobject_cast<QMenu*>(QApplication::activePopupWidget()), settingsMenu);

        constexpr UINT keyboardHookMessage = WM_APP + 0x4F;
        QVERIFY(PostMessageW(reinterpret_cast<HWND>(window.winId()), keyboardHookMessage,
                             VK_F1, 0));

        QTRY_COMPARE(engine.state().phase(), GamePhase::Bidding);
        QTRY_VERIFY(!QApplication::activePopupWidget());
        QCOMPARE(window.menuBar()->activeAction(), nullptr);
        QTRY_VERIFY(QApplication::focusWidget());
        QVERIFY(!qobject_cast<QMenuBar*>(QApplication::focusWidget()));
        QVERIFY(!qobject_cast<QMenu*>(QApplication::focusWidget()));
#else
        QSKIP("Windows native keyboard hook path only");
#endif
    }

    void testNativeF5OpensSettingsEvenWhenMenuIsOpen() {
#ifdef Q_OS_WIN
        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        auto* settingsMenu = window.findChild<QMenu*>(QStringLiteral("settingsMenu"));
        QVERIFY(settingsMenu);
        settingsMenu->menuAction()->trigger();
        QTRY_COMPARE(qobject_cast<QMenu*>(QApplication::activePopupWidget()), settingsMenu);

        bool sawSettingsDialog = false;
        QTimer closeSettingsDialog;
        closeSettingsDialog.setInterval(20);
        connect(&closeSettingsDialog, &QTimer::timeout, [&]() {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog || !dialog->windowTitle().contains(QString::fromUtf8(u8"设置"))) {
                return;
            }
            sawSettingsDialog = true;
            closeSettingsDialog.stop();
            dialog->reject();
        });
        closeSettingsDialog.start();

        constexpr UINT keyboardHookMessage = WM_APP + 0x4F;
        QVERIFY(PostMessageW(reinterpret_cast<HWND>(window.winId()), keyboardHookMessage,
                             VK_F5, 0));

        QTRY_VERIFY(sawSettingsDialog);
        QTRY_VERIFY(!QApplication::activePopupWidget());
        QCOMPARE(window.menuBar()->activeAction(), nullptr);
#else
        QSKIP("Windows native keyboard hook path only");
#endif
    }

    void testPickedCardsAreExcludedFromBrowseCountsUntilReleased() {
        HandListModel model;
        const std::vector<Card> cards = {
            Card::create(Rank::Three, Suit::Spades, 0),
            Card::create(Rank::Three, Suit::Hearts, 0),
            Card::create(Rank::Four, Suit::Spades, 0)
        };
        QVERIFY(model.setCards(cards));
        QVERIFY(model.selectSingle(0));
        QCOMPARE(model.unselectedCountOfRank(Rank::Three), 1);
        QCOMPARE(model.firstUnselectedRow(), 1);
        QCOMPARE(model.nextBrowsableGroupStartRow(0), 2);
        QCOMPARE(model.data(model.index(1, 0), Qt::AccessibleTextRole).toString(),
                 QString::fromUtf8(u8"1张3"));
        model.setSelected(0, false);
        QCOMPARE(model.unselectedCountOfRank(Rank::Three), 2);
        QCOMPARE(model.firstUnselectedRow(), 0);
    }

    void testNativeBrowseSkipsPickedCardsUntilReleased() {
#ifdef Q_OS_WIN
        GameEngine engine;
        auto& state = engine.state();
        auto& fullState = state.fullState();
        state.setPhase(GamePhase::Playing);
        fullState.currentPlayer = PlayerId::Player1;
        fullState.players[0].hand.clear();
        fullState.players[0].hand.addCards({
            Card::create(Rank::Three, Suit::Spades, 0),
            Card::create(Rank::Three, Suit::Hearts, 0),
            Card::create(Rank::Four, Suit::Spades, 0)
        });
        fullState.players[0].hand.sortByRank();

        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.refreshFromState();

        auto* handView = window.findChild<QListView*>();
        QVERIFY(handView);
        auto* handModel = qobject_cast<HandListModel*>(handView->model());
        QVERIFY(handModel);
        constexpr UINT keyboardHookMessage = WM_APP + 0x4F;
        constexpr LPARAM shiftFlag = 0x02;
        const HWND windowHandle = reinterpret_cast<HWND>(window.winId());

        QVERIFY(PostMessageW(windowHandle, keyboardHookMessage, VK_HOME, 0));
        QTRY_COMPARE(handView->currentIndex().row(), 0);
        QVERIFY(handView->selectionModel()->selectedIndexes().isEmpty());
        QVERIFY(PostMessageW(windowHandle, keyboardHookMessage, VK_UP, 0));
        QTRY_COMPARE(handModel->selectedCount(), 1);
        QTRY_VERIFY(handModel->isSelected(0));

        QVERIFY(PostMessageW(windowHandle, keyboardHookMessage, VK_HOME, 0));
        QTRY_COMPARE(handView->currentIndex().row(), 1);
        QVERIFY(handView->selectionModel()->selectedIndexes().isEmpty());
        QVERIFY(PostMessageW(windowHandle, keyboardHookMessage, VK_LEFT, 0));
        QTRY_COMPARE(handView->currentIndex().row(), 1);
        QVERIFY(PostMessageW(windowHandle, keyboardHookMessage, VK_LEFT, shiftFlag));
        QTRY_COMPARE(handView->currentIndex().row(), 1);
        QVERIFY(PostMessageW(windowHandle, keyboardHookMessage, VK_END, 0));
        QTRY_COMPARE(handView->currentIndex().row(), 2);
        QVERIFY(handView->selectionModel()->selectedIndexes().isEmpty());
        QVERIFY(PostMessageW(windowHandle, keyboardHookMessage, VK_LEFT, 0));
        QTRY_COMPARE(handView->currentIndex().row(), 1);

        QVERIFY(PostMessageW(windowHandle, keyboardHookMessage, VK_DOWN, 0));
        QTRY_COMPARE(handModel->selectedCount(), 0);
        QVERIFY(PostMessageW(windowHandle, keyboardHookMessage, VK_HOME, 0));
        QTRY_COMPARE(handView->currentIndex().row(), 0);
#else
        QSKIP("Windows native keyboard hook path only");
#endif
    }

    void testPlayerNameDialogIsAccessibleAndSaves() {
        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        auto* action = window.findChild<QAction*>(QStringLiteral("playerNameAction1"));
        QVERIFY(action);

        bool inspected = false;
        QTimer inspector;
        inspector.setInterval(20);
        connect(&inspector, &QTimer::timeout, [&]() {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto* edit = dialog->findChild<QLineEdit*>(QStringLiteral("playerNameEdit"));
            if (!edit || !edit->hasFocus()) return;
            QVERIFY(edit->accessibleName().contains(QString::fromUtf8(u8"玩家一名称")));
            QVERIFY(!edit->accessibleDescription().isEmpty());
            edit->setText(QString::fromUtf8(u8"测试东家"));
            inspected = true;
            inspector.stop();
            dialog->accept();
        });
        inspector.start();
        action->trigger();
        QVERIFY(inspected);

        auto* statusLabel = window.statusBar()->findChild<QLabel*>();
        QVERIFY(statusLabel);
        QTest::keyClick(&window, Qt::Key_1);
        QVERIFY(statusLabel->text().contains(QString::fromUtf8(u8"测试东家")));
        QVERIFY(!statusLabel->text().contains(QString::fromUtf8(u8"玩家一")));
    }

    void testCardSelectionSoundsMatchWanerbaResources() {
        QCOMPARE(SoundService::soundFileName(SoundId::CardSelect),
                 QStringLiteral("card_four/up.wav"));
        QCOMPARE(SoundService::soundFileName(SoundId::CardDeselect),
                 QStringLiteral("card_four/move.wav"));
    }

    void testF12SaysEmptyBeforeAnyCardsArePlayed() {
        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        auto* statusLabel = window.statusBar()->findChild<QLabel*>();
        QVERIFY(statusLabel);
        QTest::keyClick(&window, Qt::Key_F12);
        QCOMPARE(statusLabel->text(), QString::fromUtf8(u8"空"));
    }

    void testLegacyAutosaveIsDeletedAndEscapeAbandonsRound() {
        DataPaths::ensureDirectories();
        QFile autoSave(DataPaths::autoSaveFile());
        QVERIFY(autoSave.open(QIODevice::WriteOnly));
        autoSave.write("legacy");
        autoSave.close();
        QFile backup(DataPaths::autoSaveBackupFile());
        QVERIFY(backup.open(QIODevice::WriteOnly));
        backup.write("legacy");
        backup.close();

        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        QVERIFY(!QFile::exists(DataPaths::autoSaveFile()));
        QVERIFY(!QFile::exists(DataPaths::autoSaveBackupFile()));
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QTest::keyClick(&window, Qt::Key_F1);
        QCOMPARE(engine.state().phase(), GamePhase::Bidding);
        QTest::keyClick(&window, Qt::Key_Escape);
        QCOMPARE(engine.state().phase(), GamePhase::NotStarted);
        QCOMPARE(engine.fullState().players[0].hand.size(), 0);
    }

    void testFinishedRoundUsesPersistentBrowsableResultUntilEscape() {
        DataPaths::ensureDirectories();
        AppSettings settings;
        settings.soundEnabled = false;
        QFile settingsFile(DataPaths::settingsFile());
        QVERIFY(settingsFile.open(QIODevice::WriteOnly));
        settingsFile.write(QJsonDocument(settings.toJson()).toJson());
        settingsFile.close();

        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QTest::keyClick(&window, Qt::Key_F1);

        auto& state = engine.state().fullState();
        engine.state().setPhase(GamePhase::Playing);
        state.currentPlayer = PlayerId::Player1;
        state.players[0].role = Role::Landlord;
        for (int index = 1; index < PLAYER_COUNT; ++index) {
            state.players[static_cast<size_t>(index)].role = Role::Farmer;
        }
        state.players[0].hand.clear();
        state.players[0].hand.addCard(Card::create(Rank::Three, Suit::Spades, 0));
        state.lastPlayedCards.clear();
        state.baseScore = 3;
        window.refreshFromState();

        QTest::keyClick(&window, Qt::Key_Up);
        QTest::keyClick(&window, Qt::Key_Return);
        QTRY_COMPARE(engine.state().phase(), GamePhase::Finished);
        QTRY_VERIFY(QApplication::activeModalWidget());
        auto* resultList = QApplication::activeModalWidget()->findChild<QListWidget*>(
            QStringLiteral("roundResultList"));
        QVERIFY(resultList);
        QCOMPARE(resultList->count(), 9);
        const int firstRow = resultList->currentRow();
        QTest::keyClick(resultList, Qt::Key_Down);
        QCOMPARE(resultList->currentRow(), firstRow + 1);
        QTest::keyClick(resultList, Qt::Key_Escape);
        QTRY_COMPARE(engine.state().phase(), GamePhase::NotStarted);
        QVERIFY(!QApplication::activeModalWidget());
    }

    void testF1StartsPausesResumesAndStartsNextGame() {
        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        QTest::keyClick(&window, Qt::Key_F1);
        QCOMPARE(engine.state().phase(), GamePhase::Bidding);

        auto* handView = window.findChild<QListView*>();
        QVERIFY(handView);
        handView->setFocus(Qt::OtherFocusReason);
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget*>(handView));

        QTest::qWait(200);
        QTest::keyClick(handView, Qt::Key_F1);
        QCOMPARE(engine.state().phase(), GamePhase::Paused);
        QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(handView));

        QTest::qWait(200);
        QTest::keyClick(handView, Qt::Key_F1);
        QCOMPARE(engine.state().phase(), GamePhase::Bidding);
        QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(handView));

        const auto previousGameId = engine.state().gameId();
        engine.state().setPhase(GamePhase::Finished);
        window.refreshFromState();

        QTest::qWait(200);
        QTest::keyClick(&window, Qt::Key_F1);
        QCOMPARE(engine.state().phase(), GamePhase::Bidding);
        QVERIFY(engine.state().gameId() > previousGameId);
    }

    void testFirstHandBrowseKeepsFocusOutOfTheList() {
        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        QTest::keyClick(&window, Qt::Key_F1);
        QCOMPARE(engine.state().phase(), GamePhase::Bidding);

        auto* handView = window.findChild<QListView*>();
        QVERIFY(handView);
        QVERIFY(handView->model());
        QVERIFY(handView->model()->rowCount() > 0);

        window.setFocus(Qt::OtherFocusReason);
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget*>(&window));
        QTest::keyClick(&window, Qt::Key_Home);

        QCOMPARE(handView->currentIndex().row(), 0);
        QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(&window));
    }

    void testPrimaryKeyboardShortcuts() {
        writeCustomPlayerNames();
        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        bool sawSettingsDialog = false;
        QTimer closeSettingsDialog;
        closeSettingsDialog.setInterval(20);
        connect(&closeSettingsDialog, &QTimer::timeout, [&]() {
            QWidget* modal = QApplication::activeModalWidget();
            if (modal) {
                sawSettingsDialog = modal->windowTitle().contains(QString::fromUtf8(u8"设置"));
                modal->close();
                closeSettingsDialog.stop();
            }
        });
        closeSettingsDialog.start();
        QTest::keyClick(&window, Qt::Key_F5);
        QVERIFY(sawSettingsDialog);

        QTest::keyClick(&window, Qt::Key_F1);
        QCOMPARE(engine.state().phase(), GamePhase::Bidding);
        auto* statusLabel = window.statusBar()->findChild<QLabel*>();
        auto* bottomCardsLabel = window.findChild<QLabel*>(QStringLiteral("bottomCardsLabel"));
        QVERIFY(statusLabel);
        QVERIFY(bottomCardsLabel);
        QVERIFY(!bottomCardsLabel->isVisible());
        QVERIFY(!engine.publicSnapshot().bottomCardsRevealed);
        QVERIFY(engine.publicSnapshot().bottomCards.empty());

        QTest::keyClick(&window, Qt::Key_F2);
        QVERIFY(statusLabel->text().contains(QString::fromUtf8(u8"底牌尚未公开")));
        statusLabel->setText(QString::fromUtf8(u8"快捷键已释放"));
        QTest::keyClick(&window, Qt::Key_D, Qt::AltModifier);
        QCOMPARE(statusLabel->text(), QString::fromUtf8(u8"快捷键已释放"));

        auto& fs = engine.state().fullState();
        fs.currentPlayer = PlayerId::Player1;
        fs.highestBid = 0;
        fs.highestBidder = PlayerId::Player1;
        fs.biddingPlayerCount = 0;
        for (auto& player : fs.players) {
            player.bidScore = 0;
            player.hasPassedBid = false;
        }
        window.refreshFromState();

        QTest::keyClick(&window, Qt::Key_3);
        QCOMPARE(engine.state().phase(), GamePhase::Playing);
        QVERIFY(bottomCardsLabel->isVisible());
        QVERIFY(engine.publicSnapshot().bottomCardsRevealed);
        QCOMPARE(static_cast<int>(engine.publicSnapshot().bottomCards.size()), BOTTOM_CARDS);
        QCOMPARE(fs.currentPlayer, PlayerId::Player1);
        QCOMPARE(fs.players[0].role, Role::Landlord);
        QCOMPARE(fs.players[0].hand.size(), LANDLORD_TOTAL);

        statusLabel->setText(QString::fromUtf8(u8"Alt数字快捷键已取消"));
        QTest::keyClick(&window, Qt::Key_1, Qt::AltModifier);
        QCOMPARE(statusLabel->text(), QString::fromUtf8(u8"Alt数字快捷键已取消"));

        QTest::keyClick(&window, Qt::Key_1);
        QVERIFY(statusLabel->text().contains(QString::fromUtf8(u8"东风")));
        QVERIFY(statusLabel->text().contains(QString::fromUtf8(u8"自己")));
        QVERIFY(!statusLabel->text().contains(QString::fromUtf8(u8"玩家一")));
        QTest::keyClick(&window, Qt::Key_2);
        QVERIFY(statusLabel->text().contains(QString::fromUtf8(u8"南风")));
        QVERIFY(!statusLabel->text().contains(QString::fromUtf8(u8"玩家二")));
        QTest::keyClick(&window, Qt::Key_3);
        QVERIFY(statusLabel->text().contains(QString::fromUtf8(u8"西风")));
        QVERIFY(!statusLabel->text().contains(QString::fromUtf8(u8"玩家三")));
        QTest::keyClick(&window, Qt::Key_4);
        QVERIFY(statusLabel->text().contains(QString::fromUtf8(u8"北风")));
        QVERIFY(!statusLabel->text().contains(QString::fromUtf8(u8"玩家四")));
        QTest::keyClick(&window, Qt::Key_F2);
        QVERIFY(statusLabel->text().contains(QString::fromUtf8(u8"底牌")));
        QTest::keyClick(&window, Qt::Key_F, Qt::AltModifier);
        QVERIFY(statusLabel->text().contains(QString::fromUtf8(u8"基础分")));
        QTest::keyClick(&window, Qt::Key_F11);
        QCOMPARE(statusLabel->text(), QString::fromUtf8(u8"东风"));
        QVERIFY(!containsForbiddenSpeech(statusLabel->text()));

        auto* handView = window.findChild<QListView*>();
        QVERIFY(handView);
        auto* handModel = qobject_cast<HandListModel*>(handView->model());
        QVERIFY(handModel);
        const auto firstDisplayText = handModel->data(handModel->index(0, 0), Qt::DisplayRole).toString();
        QVERIFY(!firstDisplayText.isEmpty());
        QVERIFY(!containsForbiddenSpeech(firstDisplayText));
        QCOMPARE(handView->currentIndex().row(), 0);
        QVERIFY(handView->selectionModel()->selectedIndexes().isEmpty());
        QTest::keyClick(&window, Qt::Key_Right);
        QVERIFY(handView->currentIndex().row() > 0);
        QVERIFY(handView->selectionModel()->selectedIndexes().isEmpty());
        QVERIFY(!statusLabel->text().isEmpty());
        QVERIFY(!containsForbiddenSpeech(statusLabel->text()));
        QTest::keyClick(&window, Qt::Key_Left);
        QCOMPARE(handView->currentIndex().row(), 0);
        QVERIFY(!containsForbiddenSpeech(statusLabel->text()));
        QTest::keyClick(&window, Qt::Key_End);
        QVERIFY(handView->currentIndex().row() > 0);
        QVERIFY(handView->selectionModel()->selectedIndexes().isEmpty());
        QVERIFY(!containsForbiddenSpeech(statusLabel->text()));
        QTest::keyClick(&window, Qt::Key_Home);
        QCOMPARE(handView->currentIndex().row(), 0);
        QTest::keyClick(&window, Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(handView->currentIndex().row(), 1);
        QVERIFY(handView->selectionModel()->selectedIndexes().isEmpty());
        QVERIFY(!statusLabel->text().isEmpty());
        QVERIFY(!containsForbiddenSpeech(statusLabel->text()));
        QTest::keyClick(&window, Qt::Key_Home);

        const int handSizeBeforePlay = fs.players[0].hand.size();
        QTest::keyClick(&window, Qt::Key_Up);
        QVERIFY(handView->selectionModel()->selectedIndexes().isEmpty());
        QVERIFY(!containsForbiddenSpeech(statusLabel->text()));
        QTest::keyClick(&window, Qt::Key_Right, Qt::ShiftModifier);
        QTest::keyClick(&window, Qt::Key_Up);
        QVERIFY(!containsForbiddenSpeech(statusLabel->text()));
        QCOMPARE(handModel->selectedCount(), 2);
        QTest::keyClick(&window, Qt::Key_Down);
        QVERIFY(!containsForbiddenSpeech(statusLabel->text()));
        QCOMPARE(handModel->selectedCount(), 1);
        QTest::keyClick(&window, Qt::Key_Down);
        QCOMPARE(handModel->selectedCount(), 0);
        QTest::keyClick(&window, Qt::Key_Up, Qt::ControlModifier);
        QVERIFY(!containsForbiddenSpeech(statusLabel->text()));
        QVERIFY(handModel->selectedCount() >= 1);
        QTest::keyClick(&window, Qt::Key_Down, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 0);

        QTest::keyClick(&window, Qt::Key_Home);
        const int rowBeforePlay = handView->currentIndex().row();
        QTest::keyClick(&window, Qt::Key_Up);
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(fs.players[0].hand.size(), handSizeBeforePlay - 1);
        QTRY_VERIFY(statusLabel->text().startsWith(QString::fromUtf8(u8"东风，")));
        const int expectedRowAfterPlay = rowBeforePlay < handModel->rowCount()
            ? rowBeforePlay
            : handModel->rowCount() - 1;
        QCOMPARE(handView->currentIndex().row(), expectedRowAfterPlay);
        const QString remainingCardSpeech = handModel->data(
            handView->currentIndex(), Qt::AccessibleTextRole).toString();
        QVERIFY(remainingCardSpeech.isEmpty());
        QVERIFY(!statusLabel->text().contains(QString::fromUtf8(u8"\u51fa\u4e86")));
        QTest::keyClick(&window, Qt::Key_F12);
        QVERIFY(statusLabel->text().startsWith(QString::fromUtf8(u8"东风，")));
        QVERIFY(statusLabel->text().contains(QString::fromUtf8(u8"张")));
        QVERIFY(!statusLabel->text().contains(QString::fromUtf8(u8"最后出牌的是")));
        QVERIFY(!containsForbiddenSpeech(statusLabel->text()));

        fs.lastPlayedBy = PlayerId::Player4;
        fs.lastPlayedCards = {
            Card::create(Rank::Three, Suit::Spades, 0),
            Card::create(Rank::Three, Suit::Hearts, 0),
            Card::create(Rank::Three, Suit::Clubs, 0),
            Card::create(Rank::Three, Suit::Diamonds, 0)
        };
        QTest::keyClick(&window, Qt::Key_F12);
        QCOMPARE(statusLabel->text(), QString::fromUtf8(u8"北风，四个三，枪"));

        const int handSizeBeforePass = fs.players[0].hand.size();
        fs.currentPlayer = PlayerId::Player1;
        fs.lastPlayedBy = PlayerId::Player2;
        fs.lastPlayedCards = {fs.players[1].hand.cards().front()};
        fs.consecutivePasses = 0;
        window.refreshFromState();

        QTest::keyClick(&window, Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(fs.consecutivePasses, 0);
        QCOMPARE(fs.currentPlayer, PlayerId::Player1);
        QTest::keyClick(&window, Qt::Key_Space);
        QCOMPARE(fs.players[0].hand.size(), handSizeBeforePass);
        QCOMPARE(fs.consecutivePasses, 1);
        QCOMPARE(fs.currentPlayer, PlayerId::Player2);
        QVERIFY(!statusLabel->text().contains(QString::fromUtf8(u8"过牌")));
    }

    void testNativePlainNumberQueriesPlayersAndAltNumberIsReleased() {
#ifdef Q_OS_WIN
        writeCustomPlayerNames();
        GameEngine engine;
        engine.state().setPhase(GamePhase::Playing);
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        auto* statusLabel = window.statusBar()->findChild<QLabel*>();
        QVERIFY(statusLabel);
        constexpr UINT keyboardHookMessage = WM_APP + 0x4F;
        constexpr LPARAM altFlag = 0x04;
        const HWND windowHandle = reinterpret_cast<HWND>(window.winId());

        QVERIFY(PostMessageW(windowHandle, keyboardHookMessage, '2', 0));
        QTRY_VERIFY(statusLabel->text().contains(QString::fromUtf8(u8"南风")));

        statusLabel->setText(QString::fromUtf8(u8"Alt数字快捷键已释放"));
        QVERIFY(PostMessageW(windowHandle, keyboardHookMessage, '2', altFlag));
        QTest::qWait(50);
        QCOMPARE(statusLabel->text(), QString::fromUtf8(u8"Alt数字快捷键已释放"));
#else
        QSKIP("Windows native keyboard hook path only");
#endif
    }

    void testNativeF11AnnouncesCurrentPlayerName() {
#ifdef Q_OS_WIN
        writeCustomPlayerNames();
        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        auto& state = engine.state();
        auto& fullState = state.fullState();
        state.setPhase(GamePhase::Playing);
        fullState.currentPlayer = PlayerId::Player2;
        window.refreshFromState();

        auto* statusLabel = window.statusBar()->findChild<QLabel*>();
        QVERIFY(statusLabel);
        constexpr UINT keyboardHookMessage = WM_APP + 0x4F;
        QVERIFY(PostMessageW(reinterpret_cast<HWND>(window.winId()), keyboardHookMessage,
                             VK_F11, 0));

        QTRY_COMPARE(statusLabel->text(), QString::fromWCharArray(L"\u5357\u98ce"));
#else
        QSKIP("Windows native keyboard hook path only");
#endif
    }

    void testFreeLeaderCanPassWithSpaceQtAndNativeKeyboardPaths() {
        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        auto& state = engine.state();
        auto& fullState = state.fullState();
        state.setPhase(GamePhase::Playing);
        fullState.currentPlayer = PlayerId::Player1;
        fullState.lastPlayedBy = PlayerId::Player1;
        fullState.lastPlayedCards.clear();
        fullState.consecutivePasses = 0;
        window.refreshFromState();

        QPushButton* passButton = nullptr;
        for (auto* button : window.findChildren<QPushButton*>()) {
            if (button->accessibleName() == QString::fromWCharArray(L"\u8fc7\u724c")) {
                passButton = button;
                break;
            }
        }
        QVERIFY(passButton);
        QVERIFY(passButton->isEnabled());

        QTest::keyClick(&window, Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(fullState.currentPlayer, PlayerId::Player1);
        QCOMPARE(fullState.consecutivePasses, 0);
        QTest::keyClick(&window, Qt::Key_Space);
        QCOMPARE(fullState.currentPlayer, PlayerId::Player2);
        QVERIFY(fullState.lastPlayedCards.empty());

#ifdef Q_OS_WIN
        fullState.currentPlayer = PlayerId::Player1;
        fullState.lastPlayedBy = PlayerId::Player1;
        fullState.lastPlayedCards.clear();
        fullState.consecutivePasses = 0;
        window.refreshFromState();

        constexpr UINT keyboardHookMessage = WM_APP + 0x4F;
        constexpr LPARAM hookCtrlFlag = 0x01;
        QVERIFY(PostMessageW(reinterpret_cast<HWND>(window.winId()), keyboardHookMessage,
                             VK_RETURN, hookCtrlFlag));
        QTest::qWait(10);
        QCOMPARE(fullState.currentPlayer, PlayerId::Player1);
        QVERIFY(PostMessageW(reinterpret_cast<HWND>(window.winId()), keyboardHookMessage,
                             VK_SPACE, 0));
        QTRY_COMPARE(fullState.currentPlayer, PlayerId::Player2);
#endif
    }

private:
    static void writeCustomPlayerNames() {
        DataPaths::ensureDirectories();
        AppSettings settings;
        settings.playerNames[0] = QString::fromUtf8(u8"东风");
        settings.playerNames[1] = QString::fromUtf8(u8"南风");
        settings.playerNames[2] = QString::fromUtf8(u8"西风");
        settings.playerNames[3] = QString::fromUtf8(u8"北风");
        QFile file(DataPaths::settingsFile());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(settings.toJson()).toJson());
    }

    static bool containsForbiddenSpeech(const QString& text) {
        return text.contains(QString::fromUtf8(u8"黑桃")) ||
               text.contains(QString::fromUtf8(u8"红心")) ||
               text.contains(QString::fromUtf8(u8"梅花")) ||
               text.contains(QString::fromUtf8(u8"方块")) ||
               text.contains(QString::fromUtf8(u8"选中")) ||
               text.contains(QString::fromUtf8(u8"未选中")) ||
               text.contains(QString::fromUtf8(u8"未选择")) ||
               text.contains(QString::fromUtf8(u8"手牌列表")) ||
               text.contains(QString::fromUtf8(u8"第一张"));
    }
};

QTEST_MAIN(TestMainWindowShortcuts)
#include "test_main_window_shortcuts.moc"
