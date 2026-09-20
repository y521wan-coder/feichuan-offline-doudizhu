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
#include <QMessageBox>
#include <QAbstractButton>
#include <QRegularExpression>
#include <QStringList>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <optional>
#include <vector>

#include "accessibility/accessibility_service.h"
#include "core/audio/card_pattern_sound_plan.h"
#include "core/engine/game_engine.h"
#include "core/model/card.h"
#include "core/rules/pattern_analyzer.h"
#include "ui/main_window.h"
#include "ui/dialogs/settings_dialog.h"
#include "ui/dialogs/shortcut_dialog.h"
#include "ui/models/hand_list_model.h"
#include "ui/widgets/card_table_widget.h"
#include "ui/sound_service.h"
#include "persistence/diagnostic_trace_service.h"
#include "persistence/data_paths.h"
#include "persistence/settings_repository.h"

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

    void testSettingsExposeTwoThreeAndFourPlayerModes() {
        AppSettings settings;
        settings.playerCount = THREE_PLAYER_COUNT;
        SettingsDialog dialog(settings);

        auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("playerCountComboBox"));
        QVERIFY(combo);
        QCOMPARE(combo->count(), 3);
        QCOMPARE(combo->itemData(0).toInt(), PLAYER_COUNT);
        QCOMPARE(combo->itemData(1).toInt(), THREE_PLAYER_COUNT);
        QCOMPARE(combo->itemData(2).toInt(), TWO_PLAYER_COUNT);
        QCOMPARE(combo->itemText(0), QString::fromUtf8(u8"四人（两副牌）"));
        QCOMPARE(combo->itemText(1), QString::fromUtf8(u8"三人（一副牌）"));
        QCOMPARE(combo->itemText(2), QString::fromUtf8(u8"二人（一副牌）"));
        QCOMPARE(combo->currentData().toInt(), THREE_PLAYER_COUNT);
        QCOMPARE(dialog.settings().playerCount, THREE_PLAYER_COUNT);
    }

    void testSelectedThreePlayerModeStartsThreePlayerDeal() {
        AppSettings selected;
        selected.playerCount = THREE_PLAYER_COUNT;
        selected.normalize();
        SettingsRepository repository;
        repository.setData(selected.toJson());
        DataPaths::ensureDirectories();
        QVERIFY(repository.save(DataPaths::settingsFile()));

        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.startNewGame();

        QCOMPARE(engine.fullState().activePlayerCount, THREE_PLAYER_COUNT);
        QCOMPARE(engine.publicSnapshot().activePlayerCount, THREE_PLAYER_COUNT);
        for (int index = 0; index < THREE_PLAYER_COUNT; ++index) {
            QCOMPARE(engine.fullState().players[index].hand.size(),
                     THREE_PLAYER_CARDS_PER_PLAYER);
        }
        QVERIFY(engine.fullState().players[3].hand.empty());
        QCOMPARE(static_cast<int>(engine.fullState().bottomCards.size()),
                 THREE_PLAYER_BOTTOM_CARDS);
        auto* table = window.findChild<CardTableWidget*>(QStringLiteral("visualCardTable"));
        QVERIFY(table);
        QCOMPARE(table->displayedOpponentBackCount(PlayerId::Player4), 0);

        AppSettings defaults;
        defaults.normalize();
        repository.setData(defaults.toJson());
        QVERIFY(repository.save(DataPaths::settingsFile()));
    }

    void testSelectedTwoPlayerModeStartsHeadToHeadDeal() {
        AppSettings selected;
        selected.playerCount = TWO_PLAYER_COUNT;
        selected.normalize();
        SettingsRepository repository;
        repository.setData(selected.toJson());
        DataPaths::ensureDirectories();
        QVERIFY(repository.save(DataPaths::settingsFile()));

        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.startNewGame();

        QCOMPARE(engine.fullState().activePlayerCount, TWO_PLAYER_COUNT);
        QCOMPARE(engine.publicSnapshot().activePlayerCount, TWO_PLAYER_COUNT);
        QCOMPARE(engine.fullState().players[0].hand.size(), TWO_PLAYER_CARDS_PER_PLAYER);
        QCOMPARE(engine.fullState().players[1].hand.size(), TWO_PLAYER_CARDS_PER_PLAYER);
        QVERIFY(engine.fullState().players[2].hand.empty());
        QVERIFY(engine.fullState().players[3].hand.empty());
        QCOMPARE(engine.fullState().players[1].seat, SeatPosition::North);
        QCOMPARE(static_cast<int>(engine.fullState().bottomCards.size()),
                 TWO_PLAYER_BOTTOM_CARDS);
        auto* table = window.findChild<CardTableWidget*>(QStringLiteral("visualCardTable"));
        QVERIFY(table);
        QCOMPARE(table->displayedOpponentBackCount(PlayerId::Player2),
                 TWO_PLAYER_CARDS_PER_PLAYER);
        QCOMPARE(table->displayedOpponentBackCount(PlayerId::Player3), 0);
        QCOMPARE(table->displayedOpponentBackCount(PlayerId::Player4), 0);

        AppSettings defaults;
        defaults.normalize();
        repository.setData(defaults.toJson());
        QVERIFY(repository.save(DataPaths::settingsFile()));
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

        announcement.text = L"3";
        announcement.category = AnnouncementCategory::CardSelection;
        QVERIFY(accessibility.announce(announcement, statusLabel));
        QVERIFY(accessibility.announce(announcement, statusLabel));
        int repeatedSelectionFocusEvents = 0;
        for (const QAccessibleEvent* event : QTestAccessibility::events()) {
            if (event->type() == QAccessible::Focus && event->object() == statusLabel) {
                ++repeatedSelectionFocusEvents;
            }
        }
        QCOMPARE(repeatedSelectionFocusEvents, 2);
        QTestAccessibility::cleanup();
    }

    void testNarratorUsesStandardAnnouncementWithoutMovingKeyboardFocus() {
        qputenv("FPDZ_TEST_NARRATOR_ACTIVE", "1");
        AccessibilityService accessibility;
        qunsetenv("FPDZ_TEST_NARRATOR_ACTIVE");

        GameEngine engine;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.setFocus();
        QTRY_VERIFY(window.hasFocus());

        auto* statusLabel = window.statusBar()->findChild<QLabel*>();
        QVERIFY(statusLabel);
        QWidget* focusBefore = QApplication::focusWidget();
        QCOMPARE(focusBefore, &window);

        QTestAccessibility::initialize();
        QTestAccessibility::clearEvents();

        Announcement announcement;
        announcement.text = L"讲述人公告测试";
        announcement.category = AnnouncementCategory::System;
        announcement.priority = AnnouncementPriority::High;
        QVERIFY(accessibility.announce(announcement, statusLabel));

        int focusEventCount = 0;
        int announcementEventCount = 0;
        for (const QAccessibleEvent* event : QTestAccessibility::events()) {
            if (event->type() == QAccessible::Focus && event->object() == statusLabel) {
                ++focusEventCount;
            }
            if (event->type() != QAccessible::Announcement ||
                event->object() != statusLabel) {
                continue;
            }
            ++announcementEventCount;
            const auto* narratorEvent =
                static_cast<const QAccessibleAnnouncementEvent*>(event);
            QCOMPARE(narratorEvent->message(), QString::fromUtf8(u8"讲述人公告测试"));
            QCOMPARE(narratorEvent->politeness(),
                     QAccessible::AnnouncementPoliteness::Assertive);
        }
        QCOMPARE(focusEventCount, 0);
        QCOMPARE(announcementEventCount, 1);
        QCOMPARE(statusLabel->accessibleName(), QString::fromUtf8(u8"讲述人公告测试"));
        QCOMPARE(QApplication::focusWidget(), focusBefore);
        QCOMPARE(QString::fromStdWString(accessibility.backendName()),
                 QString::fromUtf8(u8"Windows 讲述人（UIA 公告）"));
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
        const QString browsedSevens = handModel->data(
            handView->currentIndex(), Qt::AccessibleTextRole).toString();
        QCOMPARE(browsedSevens, QString::fromUtf8(u8"4张7"));

        QTest::keyClick(&window, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 4);
        for (int row = 6; row < 10; ++row) QVERIFY(handModel->isSelected(row));
        auto* statusLabel = window.statusBar()->findChild<QLabel*>();
        QVERIFY(statusLabel);
        QCOMPARE(statusLabel->accessibleName(), browsedSevens);

        QTest::keyClick(&window, Qt::Key_Down, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 0);
        QCOMPARE(statusLabel->accessibleName(), browsedSevens);

        QTest::keyClick(&window, Qt::Key_Home);
        const QString statusBeforeSelectionSpeech = statusLabel->text();
        QTest::keyClick(&window, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 3);
        QCOMPARE(handView->currentIndex().row(), 0);
        QCOMPARE(statusLabel->accessibleName(), QString::fromUtf8(u8"3张3"));
        QTest::keyClick(&window, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 6);
        QCOMPARE(handView->currentIndex().row(), 3);
        // 本轮首尾选牌规格变更点：333 加 444 恰好构成不带翅膀的飞机，
        // 因此整组拿牌与 Ctrl+下全部放下都改用首尾范围报牌。
        // 该用例的输入正是新规格覆盖的两端点手势，旧文案“3张4 / 3张3、3张4”
        // 按 docs\测试与验收.md 记录的理由改为“3到4飞机”；
        // 不构成牌型的混合选择仍保留旧组文案，另由
        // testControlDownKeepsGroupTextForMixedSelection 覆盖。
        QCOMPARE(statusLabel->accessibleName(), QString::fromUtf8(u8"3到4飞机"));

        QTest::keyClick(&window, Qt::Key_Down, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 0);
        QCOMPARE(statusLabel->accessibleName(), QString::fromUtf8(u8"3到4飞机"));
        QTest::keyClick(&window, Qt::Key_Up);
        QCOMPARE(handModel->selectedCount(), 1);
        QVERIFY(handModel->isSelected(3));
        QCOMPARE(handView->currentIndex().row(), 3);
        QCOMPARE(statusLabel->text(), statusBeforeSelectionSpeech);
        QCOMPARE(statusLabel->accessibleName(), QStringLiteral("4"));
        QTest::keyClick(&window, Qt::Key_Up);
        QCOMPARE(handModel->selectedCount(), 2);
        QVERIFY(handModel->isSelected(4));
        QCOMPARE(handView->currentIndex().row(), 4);
        QCOMPARE(statusLabel->text(), statusBeforeSelectionSpeech);
        QCOMPARE(statusLabel->accessibleName(), QStringLiteral("4"));
        QTest::keyClick(&window, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 3);
        QCOMPARE(handView->currentIndex().row(), 3);
        QVERIFY(handModel->isSelected(5));
        QCOMPARE(statusLabel->accessibleName(), QString::fromUtf8(u8"3张4"));

        QTest::keyClick(&window, Qt::Key_Down);
        QCOMPARE(handModel->selectedCount(), 2);
        QVERIFY(!handModel->isSelected(3));
        QVERIFY(handModel->isSelected(4));
        QVERIFY(handModel->isSelected(5));
        QCOMPARE(statusLabel->accessibleName(), QStringLiteral("4"));
        QTest::keyClick(&window, Qt::Key_Down, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 0);
        QCOMPARE(statusLabel->accessibleName(), QString::fromUtf8(u8"对4"));

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

    void testFourTopLevelMenuEntriesAndAltXStillCloses() {
        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        QVERIFY(!window.findChild<QAction*>(QStringLiteral("topLevelQuitAction")));
        QCOMPARE(window.menuBar()->actions().size(), 4);
        auto* shortcutSettings = window.findChild<QAction*>(
            QStringLiteral("shortcutSettingsMenuAction"));
        QVERIFY(shortcutSettings);
        QCOMPARE(shortcutSettings, window.menuBar()->actions()[3]);
        QVERIFY(shortcutSettings->text().contains(QString::fromUtf8(u8"快捷键设置")));
        QTest::keyClick(&window, Qt::Key_X, Qt::AltModifier);
        QTRY_VERIFY(!window.isVisible());
    }

    void testAltThenTabCyclesMenusAndShortcutSettingsEntry() {
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
        QCOMPARE(window.menuBar()->activeAction()->text(),
                 QString::fromUtf8(u8"快捷键设置"));
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

    void testShortcutSettingsEntryOpensAccessibleChineseList() {
        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        bool inspected = false;
        QTimer inspector;
        inspector.setInterval(20);
        connect(&inspector, &QTimer::timeout, [&]() {
            auto* dialog = qobject_cast<ShortcutDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto* list = dialog->findChild<QListWidget*>(
                QStringLiteral("shortcutSettingsList"));
            QVERIFY(list);
            QCOMPARE(list->count(), static_cast<int>(ShortcutSettings::ActionCount));
            QVERIFY(list->item(static_cast<int>(ShortcutAction::LastAction))
                        ->text().contains(QStringLiteral("F12")));
            QVERIFY(list->accessibleName().contains(QString::fromUtf8(u8"快捷键")));
            auto* ok = dialog->findChild<QPushButton*>(
                QStringLiteral("saveShortcutSettingsButton"));
            QVERIFY(ok);
            QCOMPARE(ok->text(), QString::fromUtf8(u8"确定"));
            QVERIFY(!dialog->findChild<QPushButton*>(
                QStringLiteral("editShortcutButton")));
            inspected = true;
            inspector.stop();
            dialog->reject();
        });
        inspector.start();

        QTest::keyPress(&window, Qt::Key_Alt);
        QTest::keyRelease(&window, Qt::Key_Alt);
        QTRY_VERIFY(window.menuBar()->hasFocus());
        QTest::keyClick(window.menuBar(), Qt::Key_Tab);
        QTest::keyClick(window.menuBar(), Qt::Key_Tab);
        QTest::keyClick(window.menuBar(), Qt::Key_Tab);
        QCOMPARE(window.menuBar()->activeAction()->objectName(),
                 QStringLiteral("shortcutSettingsMenuAction"));
        QTest::keyClick(window.menuBar(), Qt::Key_Return);
        QVERIFY(inspected);
    }

    void testShortcutDialogChangesKeyAndShowsChineseConflict() {
        ShortcutSettings settings;
        ShortcutDialog dialog(settings);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));
        auto* list = dialog.findChild<QListWidget*>(
            QStringLiteral("shortcutSettingsList"));
        QVERIFY(list);

        list->setCurrentRow(static_cast<int>(ShortcutAction::LastAction));
        QTimer::singleShot(50, []() {
            auto* selection = QApplication::activeModalWidget();
            QVERIFY(selection);
            QCOMPARE(selection->windowTitle(), QString::fromUtf8(u8"设置新快捷键"));
            auto* keys = selection->findChild<QListWidget*>(
                QStringLiteral("shortcutKeySelectionList"));
            auto* status = selection->findChild<QLabel*>(
                QStringLiteral("shortcutSelectionStatus"));
            auto* useButton = selection->findChild<QPushButton*>(
                QStringLiteral("useSelectedShortcutButton"));
            QVERIFY(keys);
            QVERIFY(status);
            QVERIFY(useButton);
            QVERIFY(keys->hasFocus());
            int targetRow = -1;
            for (int row = 0; row < keys->count(); ++row) {
                if (keys->item(row)->text() == QString::fromUtf8(u8"下翻页键")) {
                    targetRow = row;
                    break;
                }
            }
            QVERIFY(targetRow >= 0);
            while (keys->currentRow() < targetRow) QTest::keyClick(keys, Qt::Key_Down);
            while (keys->currentRow() > targetRow) QTest::keyClick(keys, Qt::Key_Up);
            QTest::keyClick(keys, Qt::Key_Space);
            QCOMPARE(keys->currentItem()->checkState(), Qt::Checked);
            QVERIFY(status->text().contains(QString::fromUtf8(u8"当前选择：下翻页键")));
            QVERIFY(useButton->isEnabled());
            QTest::keyClick(keys, Qt::Key_Tab);
            QVERIFY(useButton->hasFocus());
            QTest::keyClick(useButton, Qt::Key_Return);
        });
        QTest::keyClick(list, Qt::Key_Return);
        QVERIFY(list->currentItem()->text().contains(QString::fromUtf8(u8"下翻页键")));
        QCOMPARE(dialog.settings().binding(ShortcutAction::LastAction).key,
                 static_cast<int>(Qt::Key_PageDown));

        bool sawConflict = false;
        QTimer conflictInspector;
        conflictInspector.setInterval(20);
        connect(&conflictInspector, &QTimer::timeout, [&]() {
            auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!message || message->windowTitle() != QString::fromUtf8(u8"快捷键冲突")) {
                return;
            }
            QVERIFY(message->text().contains(QString::fromUtf8(u8"复读上一条出牌")));
            QVERIFY(message->text().contains(QString::fromUtf8(u8"下翻页键")));
            sawConflict = true;
            conflictInspector.stop();
            message->accept();
        });
        conflictInspector.start();
        list->setCurrentRow(static_cast<int>(ShortcutAction::BottomCards));
        QTimer::singleShot(50, []() {
            auto* selection = QApplication::activeModalWidget();
            QVERIFY(selection);
            auto* keys = selection->findChild<QListWidget*>(
                QStringLiteral("shortcutKeySelectionList"));
            auto* useButton = selection->findChild<QPushButton*>(
                QStringLiteral("useSelectedShortcutButton"));
            QVERIFY(keys);
            QVERIFY(useButton);
            int targetRow = -1;
            for (int row = 0; row < keys->count(); ++row) {
                if (keys->item(row)->text() == QString::fromUtf8(u8"下翻页键")) {
                    targetRow = row;
                    break;
                }
            }
            QVERIFY(targetRow >= 0);
            while (keys->currentRow() < targetRow) QTest::keyClick(keys, Qt::Key_Down);
            while (keys->currentRow() > targetRow) QTest::keyClick(keys, Qt::Key_Up);
            QTest::keyClick(keys, Qt::Key_Space);
            QVERIFY(useButton->isEnabled());
            QTest::keyClick(keys, Qt::Key_Tab);
            QTest::keyClick(useButton, Qt::Key_Return);
        });
        QTest::keyClick(list, Qt::Key_Return);
        QVERIFY(sawConflict);
        QCOMPARE(dialog.settings().binding(ShortcutAction::BottomCards).key,
                 static_cast<int>(Qt::Key_F2));
    }

    void testShortcutDialogSelectsCombinationWithUpDownAndSpace() {
        ShortcutDialog dialog(ShortcutSettings{});
        dialog.show();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));
        auto* list = dialog.findChild<QListWidget*>(
            QStringLiteral("shortcutSettingsList"));
        QVERIFY(list);
        list->setCurrentRow(static_cast<int>(ShortcutAction::LastAction));

        QTimer::singleShot(50, []() {
            auto* selection = QApplication::activeModalWidget();
            QVERIFY(selection);
            auto* keys = selection->findChild<QListWidget*>(
                QStringLiteral("shortcutKeySelectionList"));
            auto* status = selection->findChild<QLabel*>(
                QStringLiteral("shortcutSelectionStatus"));
            auto* useButton = selection->findChild<QPushButton*>(
                QStringLiteral("useSelectedShortcutButton"));
            QVERIFY(keys);
            QVERIFY(status);
            QVERIFY(useButton);

            auto moveTo = [keys](const QString& text) {
                int targetRow = -1;
                for (int row = 0; row < keys->count(); ++row) {
                    if (keys->item(row)->text() == text) {
                        targetRow = row;
                        break;
                    }
                }
                QVERIFY(targetRow >= 0);
                while (keys->currentRow() < targetRow) QTest::keyClick(keys, Qt::Key_Down);
                while (keys->currentRow() > targetRow) QTest::keyClick(keys, Qt::Key_Up);
            };

            moveTo(QString::fromUtf8(u8"下翻页键"));
            QTest::keyClick(keys, Qt::Key_Space);
            QCOMPARE(keys->currentItem()->checkState(), Qt::Checked);
            moveTo(QStringLiteral("Control"));
            QTest::keyClick(keys, Qt::Key_Space);
            QCOMPARE(keys->currentItem()->checkState(), Qt::Checked);
            QCOMPARE(status->text(), QString::fromUtf8(u8"当前选择：Control加下翻页键"));
            QTest::keyClick(keys, Qt::Key_Tab);
            QVERIFY(useButton->hasFocus());
            QTest::keyClick(useButton, Qt::Key_Return);
        });

        QTest::keyClick(list, Qt::Key_Return);
        QCOMPARE(dialog.settings().binding(ShortcutAction::LastAction).key,
                 static_cast<int>(Qt::Key_PageDown));
        QCOMPARE(dialog.settings().binding(ShortcutAction::LastAction).modifiers,
                 Qt::KeyboardModifiers(Qt::ControlModifier));
        QCOMPARE(ShortcutSettings::keyName(
                     dialog.settings().binding(ShortcutAction::LastAction)),
                 QString::fromUtf8(u8"Control加下翻页键"));
    }

    void testShortcutDialogSelectsUpArrowAsSingleKey() {
        ShortcutSettings settings;
        settings.setBinding(ShortcutAction::PickCard,
                            {Qt::Key_PageDown, Qt::NoModifier});
        ShortcutDialog dialog(settings);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));
        auto* actions = dialog.findChild<QListWidget*>(
            QStringLiteral("shortcutSettingsList"));
        QVERIFY(actions);
        actions->setCurrentRow(static_cast<int>(ShortcutAction::PickCard));

        QTimer::singleShot(50, []() {
            auto* selection = QApplication::activeModalWidget();
            QVERIFY(selection);
            auto* keys = selection->findChild<QListWidget*>(
                QStringLiteral("shortcutKeySelectionList"));
            auto* useButton = selection->findChild<QPushButton*>(
                QStringLiteral("useSelectedShortcutButton"));
            QVERIFY(keys);
            QVERIFY(useButton);
            int targetRow = -1;
            for (int row = 0; row < keys->count(); ++row) {
                if (keys->item(row)->text() == QString::fromUtf8(u8"上光标键")) {
                    targetRow = row;
                    break;
                }
            }
            QVERIFY(targetRow >= 0);
            while (keys->currentRow() < targetRow) QTest::keyClick(keys, Qt::Key_Down);
            while (keys->currentRow() > targetRow) QTest::keyClick(keys, Qt::Key_Up);
            QTest::keyClick(keys, Qt::Key_Space);
            QCOMPARE(keys->currentItem()->checkState(), Qt::Checked);
            QTest::keyClick(keys, Qt::Key_Tab);
            QVERIFY(useButton->hasFocus());
            QTest::keyClick(useButton, Qt::Key_Return);
        });

        QTest::keyClick(actions, Qt::Key_Return);
        QCOMPARE(dialog.settings().binding(ShortcutAction::PickCard).key,
                 static_cast<int>(Qt::Key_Up));
        QCOMPARE(dialog.settings().binding(ShortcutAction::PickCard).modifiers,
                 Qt::KeyboardModifiers(Qt::NoModifier));
        QCOMPARE(ShortcutSettings::keyName(
                     dialog.settings().binding(ShortcutAction::PickCard)),
                 QString::fromUtf8(u8"上光标键"));
    }

    void testCustomizedTabTriggersGameShortcutOutsideMenus() {
        AppSettings custom;
        custom.shortcuts.setBinding(
            ShortcutAction::LastAction, {Qt::Key_Tab, Qt::NoModifier});
        custom.normalize();
        SettingsRepository repository;
        repository.setData(custom.toJson());
        DataPaths::ensureDirectories();
        QVERIFY(repository.save(DataPaths::settingsFile()));

        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto* statusLabel = window.statusBar()->findChild<QLabel*>();
        QVERIFY(statusLabel);
        statusLabel->setText(QString::fromUtf8(u8"尚未触发"));
        QTest::keyClick(&window, Qt::Key_Tab);
        QCOMPARE(statusLabel->text(), QString::fromUtf8(u8"空"));
        QCOMPARE(window.menuBar()->activeAction(), nullptr);

#ifdef Q_OS_WIN
        statusLabel->setText(QString::fromUtf8(u8"等待原生跳格键"));
        constexpr UINT keyboardHookMessage = WM_APP + 0x4F;
        QVERIFY(PostMessageW(reinterpret_cast<HWND>(window.winId()),
                             keyboardHookMessage, VK_TAB, 0));
        QTRY_COMPARE(statusLabel->text(), QString::fromUtf8(u8"空"));
#endif

        AppSettings defaults;
        defaults.normalize();
        repository.setData(defaults.toJson());
        QVERIFY(repository.save(DataPaths::settingsFile()));
    }

    void testCustomizedF12UsesChinesePageDownBindingInQtAndNativePaths() {
        AppSettings custom;
        custom.shortcuts.setBinding(
            ShortcutAction::LastAction,
            {Qt::Key_PageDown, Qt::NoModifier});
        custom.normalize();
        SettingsRepository repository;
        repository.setData(custom.toJson());
        DataPaths::ensureDirectories();
        QVERIFY(repository.save(DataPaths::settingsFile()));

        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto* statusLabel = window.statusBar()->findChild<QLabel*>();
        QVERIFY(statusLabel);

        statusLabel->setText(QString::fromUtf8(u8"尚未触发"));
        QTest::keyClick(&window, Qt::Key_F12);
        QCOMPARE(statusLabel->text(), QString::fromUtf8(u8"尚未触发"));
        QTest::keyClick(&window, Qt::Key_PageDown);
        QCOMPARE(statusLabel->text(), QString::fromUtf8(u8"空"));

#ifdef Q_OS_WIN
        statusLabel->setText(QString::fromUtf8(u8"等待原生按键"));
        constexpr UINT keyboardHookMessage = WM_APP + 0x4F;
        QVERIFY(PostMessageW(reinterpret_cast<HWND>(window.winId()),
                             keyboardHookMessage, VK_NEXT, 0));
        QTRY_COMPARE(statusLabel->text(), QString::fromUtf8(u8"空"));
#endif

        AppSettings defaults;
        defaults.normalize();
        repository.setData(defaults.toJson());
        QVERIFY(repository.save(DataPaths::settingsFile()));
    }

    void testCustomizedCombinationRequiresAllSelectedKeysInQtAndNativePaths() {
        AppSettings custom;
        custom.shortcuts.setBinding(
            ShortcutAction::LastAction,
            {Qt::Key_PageDown, Qt::ControlModifier});
        custom.normalize();
        SettingsRepository repository;
        repository.setData(custom.toJson());
        DataPaths::ensureDirectories();
        QVERIFY(repository.save(DataPaths::settingsFile()));

        GameEngine engine;
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto* statusLabel = window.statusBar()->findChild<QLabel*>();
        QVERIFY(statusLabel);

        statusLabel->setText(QString::fromUtf8(u8"尚未触发组合键"));
        QTest::keyClick(&window, Qt::Key_PageDown);
        QCOMPARE(statusLabel->text(), QString::fromUtf8(u8"尚未触发组合键"));
        QTest::keyClick(&window, Qt::Key_PageDown, Qt::ControlModifier);
        QCOMPARE(statusLabel->text(), QString::fromUtf8(u8"空"));

#ifdef Q_OS_WIN
        statusLabel->setText(QString::fromUtf8(u8"等待原生组合键"));
        constexpr UINT keyboardHookMessage = WM_APP + 0x4F;
        constexpr LPARAM controlFlag = 0x01;
        QVERIFY(PostMessageW(reinterpret_cast<HWND>(window.winId()),
                             keyboardHookMessage, VK_NEXT, 0));
        QTest::qWait(20);
        QCOMPARE(statusLabel->text(), QString::fromUtf8(u8"等待原生组合键"));
        QVERIFY(PostMessageW(reinterpret_cast<HWND>(window.winId()),
                             keyboardHookMessage, VK_NEXT, controlFlag));
        QTRY_COMPARE(statusLabel->text(), QString::fromUtf8(u8"空"));
#endif

        AppSettings defaults;
        defaults.normalize();
        repository.setData(defaults.toJson());
        QVERIFY(repository.save(DataPaths::settingsFile()));
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

        engine.state().fullState().currentPlayer = PlayerId::Player2;
        window.refreshFromState();

        QTimer* aiTimer = nullptr;
        const auto directTimers = window.findChildren<QTimer*>(
            QString(), Qt::FindDirectChildrenOnly);
        for (auto* timer : directTimers) {
            if (timer->isSingleShot()) {
                aiTimer = timer;
                break;
            }
        }
        QVERIFY(aiTimer);

        auto* handView = window.findChild<QListView*>();
        QVERIFY(handView);
        handView->setFocus(Qt::OtherFocusReason);
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget*>(handView));

        QTest::qWait(200);
        QTest::keyClick(handView, Qt::Key_F1);
        QCOMPARE(engine.state().phase(), GamePhase::Paused);
        QVERIFY(!aiTimer->isActive());
        QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(handView));

        QTest::qWait(200);
        QTest::keyClick(handView, Qt::Key_F1);
        QCOMPARE(engine.state().phase(), GamePhase::Bidding);
        QVERIFY(aiTimer->isActive());
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

    void testVisualCardTableUsesOnlyPublicAndHumanStateAndHasNoMouseInteraction() {
        GameEngine engine;
        auto& state = engine.state();
        auto& fullState = state.fullState();
        state.setPhase(GamePhase::Bidding);
        fullState.currentPlayer = PlayerId::Player1;
        fullState.players[0].hand.addCards({
            Card::create(Rank::Three, Suit::Spades, 0),
            Card::create(Rank::Three, Suit::Hearts, 0)
        });
        fullState.players[1].hand.addCard(Card::create(Rank::Seven, Suit::Spades, 0));
        fullState.players[2].hand.addCard(Card::create(Rank::Eight, Suit::Hearts, 0));
        fullState.players[3].hand.addCard(Card::create(Rank::Nine, Suit::Clubs, 0));
        fullState.bottomCards = {
            Card::create(Rank::Four, Suit::Spades, 0),
            Card::create(Rank::Four, Suit::Hearts, 0),
            Card::create(Rank::Five, Suit::Clubs, 0),
            Card::create(Rank::Five, Suit::Diamonds, 0),
            Card::create(Rank::Six, Suit::Spades, 0),
            Card::create(Rank::Six, Suit::Hearts, 0),
            Card::create(Rank::Jack, Suit::Clubs, 0),
            Card::create(Rank::Queen, Suit::Diamonds, 0)
        };
        fullState.bottomCardsRevealed = false;

        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.resize(1000, 760);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.refreshFromState();

        auto* table = window.findChild<CardTableWidget*>(QStringLiteral("visualCardTable"));
        auto* handView = window.findChild<QListView*>();
        auto* handModel = qobject_cast<HandListModel*>(handView ? handView->model() : nullptr);
        QVERIFY(table);
        QVERIFY(handModel);
        QCOMPARE(table->focusPolicy(), Qt::NoFocus);
        QVERIFY(table->testAttribute(Qt::WA_TransparentForMouseEvents));
        QCOMPARE(table->displayedHumanCardCount(), 2);
        QCOMPARE(table->displayedBottomCardCount(), 0);
        QCOMPARE(table->displayedLastPlayedCardCount(), 0);
        QCOMPARE(table->displayedOpponentBackCount(PlayerId::Player2), 1);
        QCOMPARE(table->displayedOpponentBackCount(PlayerId::Player3), 1);
        QCOMPARE(table->displayedOpponentBackCount(PlayerId::Player4), 1);

        state.setPhase(GamePhase::Playing);
        fullState.bottomCardsRevealed = true;
        fullState.lastPlayedBy = PlayerId::Player2;
        fullState.lastPlayedCards = {Card::create(Rank::Seven, Suit::Spades, 0)};
        window.refreshFromState();
        QCOMPARE(table->displayedBottomCardCount(), BOTTOM_CARDS);
        QCOMPARE(table->displayedLastPlayedCardCount(), 1);

        const int selectedBeforeMouse = handModel->selectedCount();
        QTest::mouseClick(table, Qt::LeftButton, Qt::NoModifier, table->rect().center());
        QCOMPARE(handModel->selectedCount(), selectedBeforeMouse);
        QCOMPARE(table->displayedSelectedCardCount(), selectedBeforeMouse);

        QTest::keyClick(&window, Qt::Key_Home);
        QTest::keyClick(&window, Qt::Key_Up);
        QCOMPARE(handModel->selectedCount(), 1);
        QCOMPARE(table->displayedSelectedCardCount(), 1);
        const QPixmap renderedTable = table->grab();
        QVERIFY(!renderedTable.isNull());
        QCOMPARE(renderedTable.size(), table->size());

        state.setPhase(GamePhase::NotStarted);
        window.close();
    }

    void testExitConfirmationUsesChineseButtonsAndDefaultsToNo() {
        GameEngine engine;
        engine.state().setPhase(GamePhase::Playing);
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        bool checkedDialog = false;
        QTimer inspectDialog;
        inspectDialog.setInterval(20);
        connect(&inspectDialog, &QTimer::timeout, [&]() {
            auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!message) return;
            QCOMPARE(message->button(QMessageBox::Yes)->text(), QString::fromUtf8(u8"是"));
            QCOMPARE(message->button(QMessageBox::No)->text(), QString::fromUtf8(u8"否"));
            QCOMPARE(message->defaultButton(), message->button(QMessageBox::No));
            QCOMPARE(message->escapeButton(), message->button(QMessageBox::No));
            checkedDialog = true;
            QTest::keyClick(message, Qt::Key_Return);
            inspectDialog.stop();
        });
        inspectDialog.start();
        window.close();

        QVERIFY(checkedDialog);
        QVERIFY(window.isVisible());
        engine.state().setPhase(GamePhase::NotStarted);
        window.close();
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
        QTRY_VERIFY(statusLabel->text().startsWith(QString::fromUtf8(u8"地主，")));
        QVERIFY(!statusLabel->text().contains(QString::fromUtf8(u8"东风")));
        const int expectedRowAfterPlay = rowBeforePlay < handModel->rowCount()
            ? rowBeforePlay
            : handModel->rowCount() - 1;
        QCOMPARE(handView->currentIndex().row(), expectedRowAfterPlay);
        const QString remainingCardSpeech = handModel->data(
            handView->currentIndex(), Qt::AccessibleTextRole).toString();
        QVERIFY(remainingCardSpeech.isEmpty());
        QVERIFY(!statusLabel->text().contains(QString::fromUtf8(u8"\u51fa\u4e86")));
        QTest::keyClick(&window, Qt::Key_F12);
        QVERIFY(statusLabel->text().startsWith(QString::fromUtf8(u8"地主，")));
        QVERIFY(!statusLabel->text().contains(QString::fromUtf8(u8"东风")));
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

        fs.players[0].role = Role::Farmer;
        fs.players[3].role = Role::Landlord;
        QTest::keyClick(&window, Qt::Key_F12);
        QCOMPARE(statusLabel->text(), QString::fromUtf8(u8"地主，四个三，枪"));
        QVERIFY(!statusLabel->text().contains(QString::fromUtf8(u8"北风")));
        fs.players[0].role = Role::Landlord;
        fs.players[3].role = Role::Farmer;

        const int handSizeBeforePass = fs.players[0].hand.size();
        fs.currentPlayer = PlayerId::Player1;
        fs.lastPlayedBy = PlayerId::Player2;
        fs.lastPlayedCards = {fs.players[1].hand.cards().front()};
        fs.consecutivePasses = 0;
        window.refreshFromState();

        QTest::keyClick(&window, Qt::Key_Space);
        QCOMPARE(fs.consecutivePasses, 0);
        QCOMPARE(fs.currentPlayer, PlayerId::Player1);
        QTest::keyClick(&window, Qt::Key_Return, Qt::ControlModifier);
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

    void testFreeLeaderCanPassWithControlEnterQtAndNativeKeyboardPaths() {
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

        QTest::keyClick(&window, Qt::Key_Space);
        QCOMPARE(fullState.currentPlayer, PlayerId::Player1);
        QCOMPARE(fullState.consecutivePasses, 0);
        QTest::keyClick(&window, Qt::Key_Return, Qt::ControlModifier);
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
                             VK_SPACE, 0));
        QTest::qWait(10);
        QCOMPARE(fullState.currentPlayer, PlayerId::Player1);
        QVERIFY(PostMessageW(reinterpret_cast<HWND>(window.winId()), keyboardHookMessage,
                             VK_RETURN, hookCtrlFlag));
        QTRY_COMPARE(fullState.currentPlayer, PlayerId::Player2);
#endif
    }

    // ===== 首尾选牌辅助：模型层确定性用例（S/P/I 系列）=====

    void testEndpointCompletionStraightForwardAndReverse() {
        // S01 先 3 后 7；S02 反向 7 后 3；S19 同一输入结果完全一致
        const auto cards = sequenceOfHand(Rank::Three, 5, 1);
        HandListModel forward;
        QVERIFY(forward.setCards(cards));
        QCOMPARE(forward.rowCount(), 5);
        QVERIFY(forward.selectSingle(0));
        QVERIFY(forward.selectSingle(4));
        const auto pattern = forward.completeEndpointSelection(PLAYER_COUNT, true);
        QVERIFY(pattern.has_value());
        QCOMPARE(pattern->type, CardPatternType::Straight);
        QCOMPARE(pattern->mainRank, Rank::Three);
        QCOMPARE(pattern->mainLength, 5);
        QCOMPARE(pattern->totalCards, 5);
        QCOMPARE(forward.selectedCount(), 5);
        const auto expectedIds = forward.selectedCardIds();

        HandListModel reverse;
        QVERIFY(reverse.setCards(cards));
        QVERIFY(reverse.selectSingle(4));
        QVERIFY(reverse.selectSingle(0));
        const auto reversePattern = reverse.completeEndpointSelection(PLAYER_COUNT, true);
        QVERIFY(reversePattern.has_value());
        QVERIFY(reverse.selectedCardIds() == expectedIds);

        HandListModel repeat;
        QVERIFY(repeat.setCards(cards));
        QVERIFY(repeat.selectSingle(0));
        QVERIFY(repeat.selectSingle(4));
        QVERIFY(repeat.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        QVERIFY(repeat.selectedCardIds() == expectedIds);
    }

    void testEndpointCompletionStraightKeepsRequestedRange() {
        // S03 手牌 3456789，端点 3/7 只补到 7
        const auto cards = sequenceOfHand(Rank::Three, 7, 1);
        HandListModel model;
        QVERIFY(model.setCards(cards));
        QVERIFY(model.selectSingle(0));
        QVERIFY(model.selectSingle(4));
        const auto pattern = model.completeEndpointSelection(PLAYER_COUNT, true);
        QVERIFY(pattern.has_value());
        QCOMPARE(model.selectedCount(), 5);
        for (const int row : {0, 1, 2, 3, 4}) QVERIFY(model.isSelected(row));
        QVERIFY(!model.isSelected(5));
        QVERIFY(!model.isSelected(6));
    }

    void testEndpointCompletionStraightRequiresMinimumLength() {
        // S04 手牌 3456，端点 3/6 长度不足
        const auto cards = sequenceOfHand(Rank::Three, 4, 1);
        HandListModel model;
        QVERIFY(model.setCards(cards));
        QVERIFY(model.selectSingle(0));
        QVERIFY(model.selectSingle(3));
        QVERIFY(!model.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        QCOMPARE(model.selectedCount(), 2);
        QVERIFY(!model.isSelected(1));
        QVERIFY(!model.isSelected(2));
    }

    void testEndpointCompletionFailsWithoutChangingSelectionOrOrder() {
        // S05 缺 6 不补；I05 失败时不发选择变化信号，拿牌队列保持原样
        const auto cards = handOfCounts({{Rank::Three, 1}, {Rank::Four, 1},
                                         {Rank::Five, 1}, {Rank::Seven, 1}});
        HandListModel model;
        QVERIFY(model.setCards(cards));
        QCOMPARE(model.rowCount(), 4);
        QVERIFY(model.selectSingle(0));
        QVERIFY(model.selectSingle(3));
        int changes = 0;
        QObject::connect(&model, &QAbstractItemModel::dataChanged,
            [&changes](const QModelIndex&, const QModelIndex&, const QVector<int>&) {
                ++changes;
            });
        QVERIFY(!model.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        QCOMPARE(changes, 0);
        QCOMPARE(model.selectedCount(), 2);
        const auto first = model.deselectNextPickedCard();
        QVERIFY(first.has_value());
        QCOMPARE(first->rank(), Rank::Three);
        const auto second = model.deselectNextPickedCard();
        QVERIFY(second.has_value());
        QCOMPARE(second->rank(), Rank::Seven);
        QCOMPARE(model.selectedCount(), 0);
    }

    void testEndpointCompletionTakesOneCardPerMiddleRank() {
        // S06 同点多张时每个中间点数只取一张
        const auto cards = handOfCounts({{Rank::Three, 3}, {Rank::Four, 2}, {Rank::Five, 1},
                                         {Rank::Six, 4}, {Rank::Seven, 2}});
        HandListModel model;
        QVERIFY(model.setCards(cards));
        QCOMPARE(model.rowCount(), 12);
        QVERIFY(model.selectSingle(0));
        QVERIFY(model.selectSingle(10));
        const auto pattern = model.completeEndpointSelection(PLAYER_COUNT, true);
        QVERIFY(pattern.has_value());
        QCOMPARE(model.selectedCount(), 5);
        QCOMPARE(model.unselectedCountOfRank(Rank::Three), 2);
        QCOMPARE(model.unselectedCountOfRank(Rank::Four), 1);
        QCOMPARE(model.unselectedCountOfRank(Rank::Five), 0);
        QCOMPARE(model.unselectedCountOfRank(Rank::Six), 3);
        QCOMPARE(model.unselectedCountOfRank(Rank::Seven), 1);
    }

    void testEndpointCompletionKeepsChosenEndpointEntities() {
        // S07 端点使用具体实体，补牌不替换成同点排序更靠前的牌
        const auto cards = handOfCounts({{Rank::Three, 3}, {Rank::Four, 2}, {Rank::Five, 1},
                                         {Rank::Six, 1}, {Rank::Seven, 2}});
        HandListModel model;
        QVERIFY(model.setCards(cards));
        QCOMPARE(model.cardAt(2).rank(), Rank::Three);
        QCOMPARE(model.cardAt(8).rank(), Rank::Seven);
        QVERIFY(model.selectSingle(2));
        QVERIFY(model.selectSingle(8));
        QVERIFY(model.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        QCOMPARE(model.selectedCount(), 5);
        QVERIFY(model.isSelected(2));
        QVERIFY(!model.isSelected(0));
        QVERIFY(!model.isSelected(1));
        QVERIFY(model.isSelected(3));
        QVERIFY(model.isSelected(5));
        QVERIFY(model.isSelected(6));
    }

    void testEndpointCompletionCoversFullThreeToAceRange() {
        // S08 3 到 A 完整十二点、10/J/Q/K/A 五点，A 不能作为低位接 2
        const auto full = sequenceOfHand(Rank::Three, 12, 1);
        HandListModel model;
        QVERIFY(model.setCards(full));
        QCOMPARE(model.rowCount(), 12);
        QVERIFY(model.selectSingle(0));
        QVERIFY(model.selectSingle(11));
        const auto pattern = model.completeEndpointSelection(PLAYER_COUNT, true);
        QVERIFY(pattern.has_value());
        QCOMPARE(pattern->type, CardPatternType::Straight);
        QCOMPARE(pattern->mainRank, Rank::Three);
        QCOMPARE(pattern->mainLength, 12);
        QCOMPARE(model.selectedCount(), 12);

        const auto high = sequenceOfHand(Rank::Ten, 5, 1);
        HandListModel highModel;
        QVERIFY(highModel.setCards(high));
        QVERIFY(highModel.selectSingle(0));
        QVERIFY(highModel.selectSingle(4));
        const auto highPattern = highModel.completeEndpointSelection(PLAYER_COUNT, true);
        QVERIFY(highPattern.has_value());
        QCOMPARE(highPattern->mainRank, Rank::Ten);
        QCOMPARE(highPattern->mainLength, 5);
        QCOMPARE(highModel.selectedCount(), 5);

        const auto aceTwo = handOfCounts({{Rank::Ace, 1}, {Rank::Two, 1}});
        HandListModel rejected;
        QVERIFY(rejected.setCards(aceTwo));
        QVERIFY(rejected.selectSingle(0));
        QVERIFY(rejected.selectSingle(1));
        QVERIFY(!rejected.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        QCOMPARE(rejected.selectedCount(), 2);
    }

    void testEndpointCompletionRejectsTwoAndJokers() {
        // S09 端点含 2 或王时不补
        std::vector<Card> cards = sequenceOfHand(Rank::Three, 12, 1);
        const auto two = handOfCounts({{Rank::Two, 1}});
        cards.insert(cards.end(), two.begin(), two.end());
        const auto smallJoker = handOfCounts({{Rank::SmallJoker, 1}});
        cards.insert(cards.end(), smallJoker.begin(), smallJoker.end());
        const auto bigJoker = handOfCounts({{Rank::BigJoker, 1}});
        cards.insert(cards.end(), bigJoker.begin(), bigJoker.end());
        HandListModel model;
        QVERIFY(model.setCards(cards));
        QCOMPARE(model.rowCount(), 15);
        int aceRow = -1;
        int twoRow = -1;
        int bigRow = -1;
        for (int row = 0; row < model.rowCount(); ++row) {
            const auto rank = model.cardAt(row).rank();
            if (rank == Rank::Ace) aceRow = row;
            if (rank == Rank::Two) twoRow = row;
            if (rank == Rank::BigJoker) bigRow = row;
        }
        QVERIFY(aceRow > 0);
        QVERIFY(twoRow > aceRow);
        QVERIFY(bigRow > twoRow);

        QVERIFY(model.selectSingle(aceRow));
        QVERIFY(model.selectSingle(twoRow));
        QVERIFY(!model.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        QCOMPARE(model.selectedCount(), 2);

        model.clearSelection();
        QVERIFY(model.selectSingle(0));
        QVERIFY(model.selectSingle(bigRow));
        QVERIFY(!model.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        QCOMPARE(model.selectedCount(), 2);

        model.clearSelection();
        QVERIFY(model.selectSingle(twoRow));
        QVERIFY(model.selectSingle(bigRow));
        QVERIFY(!model.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        QCOMPARE(model.selectedCount(), 2);
    }

    void testEndpointCompletionKeepsUnrelatedSelection() {
        // S10 存在第三种点数选择时不补、也不丢旧选择
        std::vector<Card> cards = sequenceOfHand(Rank::Three, 5, 1);
        const auto nine = handOfCounts({{Rank::Nine, 1}});
        cards.insert(cards.end(), nine.begin(), nine.end());
        HandListModel model;
        QVERIFY(model.setCards(cards));
        QCOMPARE(model.rowCount(), 6);
        const int nineRow = model.lastBrowsableGroupStartRow();
        QVERIFY(nineRow >= 0);
        QCOMPARE(model.cardAt(nineRow).rank(), Rank::Nine);
        QVERIFY(model.selectSingle(nineRow));
        QVERIFY(model.selectSingle(0));
        QVERIFY(model.selectSingle(4));
        QVERIFY(!model.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        QCOMPARE(model.selectedCount(), 3);
        QVERIFY(model.isSelected(nineRow));
        QVERIFY(model.isSelected(0));
        QVERIFY(model.isSelected(4));
    }

    void testEndpointCompletionKeepsSameRankPairUnchanged() {
        // S11 两个同点单牌保持对子
        const auto cards = handOfCounts({{Rank::Five, 2}, {Rank::Eight, 1}, {Rank::Nine, 1}});
        HandListModel model;
        QVERIFY(model.setCards(cards));
        QVERIFY(model.selectSingle(0));
        QVERIFY(model.selectSingle(1));
        QVERIFY(!model.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        QCOMPARE(model.selectedCount(), 2);
        QVERIFY(model.isSelected(0));
        QVERIFY(model.isSelected(1));
    }

    void testEndpointCompletionHonoursAllowStraightFlag() {
        // S12 单张入口关闭时不补顺子；P14 各两张首尾仍可补连对（整组入口同样成立）
        const auto singles = sequenceOfHand(Rank::Three, 5, 1);
        HandListModel singleModel;
        QVERIFY(singleModel.setCards(singles));
        QVERIFY(singleModel.selectSingle(0));
        QVERIFY(singleModel.selectSingle(4));
        QVERIFY(!singleModel.completeEndpointSelection(PLAYER_COUNT, false).has_value());
        QCOMPARE(singleModel.selectedCount(), 2);

        const auto pairs = sequenceOfHand(Rank::Four, 3, 2);
        HandListModel pairModel;
        QVERIFY(pairModel.setCards(pairs));
        QCOMPARE(pairModel.rowCount(), 6);
        QVERIFY(pairModel.selectSingle(0));
        QVERIFY(pairModel.selectSingle(1));
        QVERIFY(pairModel.selectSingle(4));
        QVERIFY(pairModel.selectSingle(5));
        const auto pattern = pairModel.completeEndpointSelection(PLAYER_COUNT, false);
        QVERIFY(pattern.has_value());
        QCOMPARE(pattern->type, CardPatternType::ConsecutivePairs);
        QCOMPARE(pairModel.selectedCount(), 6);

        HandListModel groupModel;
        QVERIFY(groupModel.setCards(pairs));
        QCOMPARE(groupModel.selectGroup(0).newlySelectedCount, 2);
        const int sixRow = groupModel.nextGroupStartRow(groupModel.nextGroupStartRow(0));
        QCOMPARE(groupModel.cardAt(sixRow).rank(), Rank::Six);
        QCOMPARE(groupModel.selectGroup(sixRow).newlySelectedCount, 2);
        QCOMPARE(groupModel.selectedCount(), 4);
        const auto groupPattern = groupModel.completeEndpointSelection(PLAYER_COUNT, false);
        QVERIFY(groupPattern.has_value());
        QCOMPARE(groupModel.selectedCount(), 6);
    }

    void testEndpointCompletionRejectsUnsupportedPlayerCounts() {
        // S13 人数 0/1/5 不改变任何状态
        const auto cards = sequenceOfHand(Rank::Three, 5, 1);
        HandListModel model;
        QVERIFY(model.setCards(cards));
        QVERIFY(model.selectSingle(0));
        QVERIFY(model.selectSingle(4));
        for (const int playerCount : {0, 1, 5}) {
            QVERIFY(!model.completeEndpointSelection(playerCount, true).has_value());
            QVERIFY(!model.completeEndpointSelection(playerCount, false).has_value());
        }
        QCOMPARE(model.selectedCount(), 2);
        QCOMPARE(model.unselectedCountOfRank(Rank::Four), 1);
    }

    void testEndpointCompletionReturnsEmptyForEmptyOrSingleSelection() {
        // S14 空手牌、空选择、仅一张选择
        HandListModel empty;
        QVERIFY(!empty.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        QCOMPARE(empty.rowCount(), 0);

        const auto cards = sequenceOfHand(Rank::Three, 5, 1);
        HandListModel model;
        QVERIFY(model.setCards(cards));
        int changes = 0;
        QObject::connect(&model, &QAbstractItemModel::dataChanged,
            [&changes](const QModelIndex&, const QModelIndex&, const QVector<int>&) {
                ++changes;
            });
        QVERIFY(!model.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        QVERIFY(model.selectSingle(0));
        QCOMPARE(model.selectedCount(), 1);
        QCOMPARE(changes, 1);
        QVERIFY(!model.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        QCOMPARE(changes, 1);
        QCOMPARE(model.selectedCount(), 1);
    }

    void testEndpointCompletionKeepsUniqueIdsInDoubleDeck() {
        // S15 四人两副牌同点同花色不同副牌编号是不同实体
        std::vector<Card> cards;
        for (int offset = 0; offset < 5; ++offset) {
            const auto rank = static_cast<Rank>(static_cast<int>(Rank::Three) + offset);
            cards.push_back(Card::create(rank, Suit::Spades, 0));
            cards.push_back(Card::create(rank, Suit::Spades, 1));
        }
        HandListModel model;
        QVERIFY(model.setCards(cards));
        QCOMPARE(model.rowCount(), 10);
        QVERIFY(model.selectSingle(0));
        QVERIFY(model.selectSingle(9));
        const auto pattern = model.completeEndpointSelection(PLAYER_COUNT, true);
        QVERIFY(pattern.has_value());
        QCOMPARE(model.selectedCount(), 5);
        auto ids = model.selectedCardIds();
        std::sort(ids.begin(), ids.end());
        QCOMPARE(static_cast<int>(std::unique(ids.begin(), ids.end()) - ids.begin()), 5);
        QVERIFY(model.isSelected(0));
        QVERIFY(model.isSelected(9));
        QVERIFY(!model.isSelected(1));
        QVERIFY(!model.isSelected(8));
    }

    void testEndpointCompletionIsIdempotent() {
        // S16 成功后重复调用不再补牌
        const auto cards = sequenceOfHand(Rank::Three, 5, 1);
        HandListModel model;
        QVERIFY(model.setCards(cards));
        QVERIFY(model.selectSingle(0));
        QVERIFY(model.selectSingle(4));
        QVERIFY(model.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        const auto ids = model.selectedCardIds();
        QVERIFY(!model.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        QVERIFY(!model.completeEndpointSelection(PLAYER_COUNT, false).has_value());
        QCOMPARE(model.selectedCount(), 5);
        QVERIFY(model.selectedCardIds() == ids);
    }

    void testReleasingMiddleCardAfterCompletionIsNotRefilled() {
        // S17 放下一张后不补回；同一牌组保持原选择
        const auto cards = sequenceOfHand(Rank::Three, 5, 1);
        HandListModel model;
        QVERIFY(model.setCards(cards));
        QVERIFY(model.selectSingle(0));
        QVERIFY(model.selectSingle(4));
        QVERIFY(model.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        const int fiveRow = model.groupStartRow(2);
        QCOMPARE(model.cardAt(fiveRow).rank(), Rank::Five);
        QVERIFY(model.isSelected(fiveRow));
        model.setSelected(fiveRow, false);
        QCOMPARE(model.selectedCount(), 4);
        QVERIFY(!model.setCards(cards));
        QCOMPARE(model.selectedCount(), 4);
        QVERIFY(!model.isSelected(fiveRow));
    }

    void testCompletedStraightIsNotExtendedByNextCard() {
        // S18 已组成 3—7 后再拿 9 不补 8
        const auto cards = sequenceOfHand(Rank::Three, 7, 1);
        HandListModel model;
        QVERIFY(model.setCards(cards));
        QVERIFY(model.selectSingle(0));
        QVERIFY(model.selectSingle(4));
        QVERIFY(model.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        QCOMPARE(model.selectedCount(), 5);
        const int nineRow = model.groupStartRow(6);
        QCOMPARE(model.cardAt(nineRow).rank(), Rank::Nine);
        QVERIFY(model.selectSingle(nineRow));
        QCOMPARE(model.selectedCount(), 6);
        QVERIFY(!model.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        QVERIFY(!model.isSelected(5));
    }

    void testSelectSingleKeepsOriginalSemantics() {
        // S20 不调用辅助方法时，连续两次单张拿牌仍然只拿两张
        const auto cards = sequenceOfHand(Rank::Three, 5, 1);
        HandListModel model;
        QVERIFY(model.setCards(cards));
        QVERIFY(model.selectSingle(0));
        QVERIFY(model.selectSingle(4));
        QCOMPARE(model.selectedCount(), 2);
        QVERIFY(!PatternAnalyzer::analyze(model.selectedCards(), PLAYER_COUNT).isValid());
    }

    void testEndpointCompletionBuildsConsecutivePairsBothDirections() {
        // P01 先 44 再 66；P02 反向顺序结果一致
        const auto cards = sequenceOfHand(Rank::Four, 3, 2);
        HandListModel forward;
        QVERIFY(forward.setCards(cards));
        QCOMPARE(forward.rowCount(), 6);
        QVERIFY(forward.selectSingle(0));
        QVERIFY(forward.selectSingle(1));
        QVERIFY(forward.selectSingle(4));
        QVERIFY(forward.selectSingle(5));
        const auto pattern = forward.completeEndpointSelection(PLAYER_COUNT, false);
        QVERIFY(pattern.has_value());
        QCOMPARE(pattern->type, CardPatternType::ConsecutivePairs);
        QCOMPARE(pattern->mainRank, Rank::Four);
        QCOMPARE(pattern->mainLength, 3);
        QCOMPARE(pattern->totalCards, 6);
        QCOMPARE(forward.selectedCount(), 6);
        const auto ids = forward.selectedCardIds();

        HandListModel reverse;
        QVERIFY(reverse.setCards(cards));
        QVERIFY(reverse.selectSingle(4));
        QVERIFY(reverse.selectSingle(5));
        QVERIFY(reverse.selectSingle(0));
        QVERIFY(reverse.selectSingle(1));
        QVERIFY(reverse.completeEndpointSelection(PLAYER_COUNT, false).has_value());
        QVERIFY(reverse.selectedCardIds() == ids);
        QVERIFY(reverse.isSelected(0));
        QVERIFY(reverse.isSelected(5));
    }

    void testConsecutivePairCompletionNeedsTwoCopiesOfEveryMiddleRank() {
        // P03 中间点数只有一张时不补
        const auto cards = handOfCounts({{Rank::Four, 2}, {Rank::Five, 1}, {Rank::Six, 2}});
        HandListModel model;
        QVERIFY(model.setCards(cards));
        QCOMPARE(model.rowCount(), 5);
        QVERIFY(model.selectSingle(0));
        QVERIFY(model.selectSingle(1));
        QVERIFY(model.selectSingle(3));
        QVERIFY(model.selectSingle(4));
        QVERIFY(!model.completeEndpointSelection(PLAYER_COUNT, false).has_value());
        QCOMPARE(model.selectedCount(), 4);
    }

    void testConsecutivePairCompletionRequiresThreePairs() {
        // P04 只有两对时不补
        const auto cards = sequenceOfHand(Rank::Four, 2, 2);
        HandListModel model;
        QVERIFY(model.setCards(cards));
        QCOMPARE(model.rowCount(), 4);
        QVERIFY(model.selectSingle(0));
        QVERIFY(model.selectSingle(1));
        QVERIFY(model.selectSingle(2));
        QVERIFY(model.selectSingle(3));
        QVERIFY(!model.completeEndpointSelection(PLAYER_COUNT, false).has_value());
        QCOMPARE(model.selectedCount(), 4);
    }

    void testConsecutivePairCompletionTakesExactlyTwoPerRank() {
        // P05 同点三张/四张时每个中间点数只补两张
        const auto cards = handOfCounts({{Rank::Four, 3}, {Rank::Five, 4}, {Rank::Six, 4}});
        HandListModel model;
        QVERIFY(model.setCards(cards));
        QCOMPARE(model.rowCount(), 11);
        QVERIFY(model.selectSingle(0));
        QVERIFY(model.selectSingle(1));
        QVERIFY(model.selectSingle(7));
        QVERIFY(model.selectSingle(8));
        const auto pattern = model.completeEndpointSelection(PLAYER_COUNT, false);
        QVERIFY(pattern.has_value());
        QCOMPARE(pattern->type, CardPatternType::ConsecutivePairs);
        QCOMPARE(model.selectedCount(), 6);
        QCOMPARE(model.unselectedCountOfRank(Rank::Four), 1);
        QCOMPARE(model.unselectedCountOfRank(Rank::Five), 2);
        QCOMPARE(model.unselectedCountOfRank(Rank::Six), 2);
    }

    void testSelectedTripleGroupIsNotShrunkIntoConsecutivePairs() {
        // P06 整组三张保持三张，不偷偷删一张
        const auto cards = handOfCounts({{Rank::Four, 3}, {Rank::Five, 2}, {Rank::Six, 2}});
        HandListModel model;
        QVERIFY(model.setCards(cards));
        QCOMPARE(model.selectGroup(0).newlySelectedCount, 3);
        int sixRow = -1;
        for (int row = 0; row < model.rowCount(); ++row) {
            if (model.cardAt(row).rank() == Rank::Six) {
                sixRow = row;
                break;
            }
        }
        QVERIFY(sixRow > 0);
        QCOMPARE(model.selectGroup(sixRow).newlySelectedCount, 2);
        QCOMPARE(model.selectedCount(), 5);
        QVERIFY(!model.completeEndpointSelection(PLAYER_COUNT, false).has_value());
        QCOMPARE(model.selectedCount(), 5);
        QVERIFY(model.isSelected(2));
        QCOMPARE(model.unselectedCountOfRank(Rank::Four), 0);
    }

    void testSingleCardsOfTwoRanksAreNotGuessedAsPairs() {
        // P07 一张 4 加一张 6 不猜成连对
        const auto cards = handOfCounts({{Rank::Four, 2}, {Rank::Five, 2}, {Rank::Six, 2}});
        HandListModel model;
        QVERIFY(model.setCards(cards));
        QVERIFY(model.selectSingle(0));
        QVERIFY(model.selectSingle(4));
        QCOMPARE(model.cardAt(4).rank(), Rank::Six);
        QVERIFY(!model.completeEndpointSelection(PLAYER_COUNT, false).has_value());
        QVERIFY(!model.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        QCOMPARE(model.selectedCount(), 2);
    }

    void testAsymmetricEndpointsDoNotComplete() {
        // P08 两张 4 加一张 6 形状不对称
        const auto cards = handOfCounts({{Rank::Four, 2}, {Rank::Five, 2}, {Rank::Six, 2}});
        HandListModel model;
        QVERIFY(model.setCards(cards));
        QVERIFY(model.selectSingle(0));
        QVERIFY(model.selectSingle(1));
        QVERIFY(model.selectSingle(4));
        QVERIFY(!model.completeEndpointSelection(PLAYER_COUNT, false).has_value());
        QCOMPARE(model.selectedCount(), 3);
    }

    void testExtraSelectionBlocksConsecutivePairCompletion() {
        // P09 额外点数阻止补牌且不被删除
        const auto cards = handOfCounts({{Rank::Four, 2}, {Rank::Five, 2},
                                         {Rank::Six, 2}, {Rank::Nine, 1}});
        HandListModel model;
        QVERIFY(model.setCards(cards));
        QVERIFY(model.selectSingle(0));
        QVERIFY(model.selectSingle(1));
        QVERIFY(model.selectSingle(4));
        QVERIFY(model.selectSingle(5));
        QVERIFY(model.selectSingle(6));
        QCOMPARE(model.cardAt(6).rank(), Rank::Nine);
        QVERIFY(!model.completeEndpointSelection(PLAYER_COUNT, false).has_value());
        QCOMPARE(model.selectedCount(), 5);
        QVERIFY(model.isSelected(6));
    }

    void testConsecutivePairCompletionAcrossFaceCardsAndRejectsTwo() {
        // P10 J—A 四对可补；端点含 2 不能跨 2
        const auto face = sequenceOfHand(Rank::Jack, 4, 2);
        HandListModel model;
        QVERIFY(model.setCards(face));
        QCOMPARE(model.rowCount(), 8);
        QVERIFY(model.selectSingle(0));
        QVERIFY(model.selectSingle(1));
        QVERIFY(model.selectSingle(6));
        QVERIFY(model.selectSingle(7));
        const auto pattern = model.completeEndpointSelection(PLAYER_COUNT, false);
        QVERIFY(pattern.has_value());
        QCOMPARE(pattern->mainRank, Rank::Jack);
        QCOMPARE(pattern->mainLength, 4);
        QCOMPARE(model.selectedCount(), 8);

        std::vector<Card> withTwo = sequenceOfHand(Rank::Queen, 3, 2);
        const auto twos = handOfCounts({{Rank::Two, 2}});
        withTwo.insert(withTwo.end(), twos.begin(), twos.end());
        HandListModel rejected;
        QVERIFY(rejected.setCards(withTwo));
        QCOMPARE(rejected.rowCount(), 8);
        int twoRow = -1;
        for (int row = 0; row < rejected.rowCount(); ++row) {
            if (rejected.cardAt(row).rank() == Rank::Two) {
                twoRow = row;
                break;
            }
        }
        QVERIFY(twoRow > 0);
        QVERIFY(rejected.selectSingle(0));
        QVERIFY(rejected.selectSingle(1));
        QVERIFY(rejected.selectSingle(twoRow));
        QVERIFY(rejected.selectSingle(twoRow + 1));
        QVERIFY(!rejected.completeEndpointSelection(PLAYER_COUNT, false).has_value());
        QCOMPARE(rejected.selectedCount(), 4);
    }

    void testFullAceChainCompletionInFourPlayerMode() {
        // P11 四人两副牌 3 到 A 各两张共 24 张
        const auto cards = sequenceOfHand(Rank::Three, 12, 2);
        HandListModel model;
        QVERIFY(model.setCards(cards));
        QCOMPARE(model.rowCount(), 24);
        QVERIFY(model.selectSingle(0));
        QVERIFY(model.selectSingle(1));
        QVERIFY(model.selectSingle(22));
        QVERIFY(model.selectSingle(23));
        const auto pattern = model.completeEndpointSelection(PLAYER_COUNT, false);
        QVERIFY(pattern.has_value());
        QCOMPARE(pattern->type, CardPatternType::ConsecutivePairs);
        QCOMPARE(pattern->mainRank, Rank::Three);
        QCOMPARE(pattern->mainLength, 12);
        QCOMPARE(pattern->totalCards, 24);
        QCOMPARE(model.selectedCount(), 24);
        auto ids = model.selectedCardIds();
        std::sort(ids.begin(), ids.end());
        QCOMPARE(static_cast<int>(std::unique(ids.begin(), ids.end()) - ids.begin()), 24);
    }

    void testTripleAndBombGroupsAreNotCompletedIntoAirplanes() {
        // P12 首尾各三张/各四张都不自动补飞机
        const auto triples = handOfCounts({{Rank::Three, 3}, {Rank::Four, 3}});
        HandListModel tripleModel;
        QVERIFY(tripleModel.setCards(triples));
        QCOMPARE(tripleModel.selectGroup(0).newlySelectedCount, 3);
        int fourRow = -1;
        for (int row = 0; row < tripleModel.rowCount(); ++row) {
            if (tripleModel.cardAt(row).rank() == Rank::Four) {
                fourRow = row;
                break;
            }
        }
        QVERIFY(fourRow > 0);
        QCOMPARE(tripleModel.selectGroup(fourRow).newlySelectedCount, 3);
        QCOMPARE(tripleModel.selectedCount(), 6);
        QVERIFY(!tripleModel.completeEndpointSelection(PLAYER_COUNT, false).has_value());
        QCOMPARE(tripleModel.selectedCount(), 6);

        const auto bombs = handOfCounts({{Rank::Three, 4}, {Rank::Four, 4}});
        HandListModel bombModel;
        QVERIFY(bombModel.setCards(bombs));
        QCOMPARE(bombModel.selectGroup(0).newlySelectedCount, 4);
        int fourRowInBombs = -1;
        for (int row = 0; row < bombModel.rowCount(); ++row) {
            if (bombModel.cardAt(row).rank() == Rank::Four) {
                fourRowInBombs = row;
                break;
            }
        }
        QVERIFY(fourRowInBombs > 0);
        QCOMPARE(bombModel.selectGroup(fourRowInBombs).newlySelectedCount, 4);
        QCOMPARE(bombModel.selectedCount(), 8);
        QVERIFY(!bombModel.completeEndpointSelection(PLAYER_COUNT, false).has_value());
        QCOMPARE(bombModel.selectedCount(), 8);
    }

    void testStraightAndPairsShapesStayDistinct() {
        // P13 1+1 只顺子、2+2 只连对
        const auto cards = sequenceOfHand(Rank::Four, 3, 2);
        HandListModel singleModel;
        QVERIFY(singleModel.setCards(cards));
        QVERIFY(singleModel.selectSingle(0));
        QVERIFY(singleModel.selectSingle(4));
        QVERIFY(!singleModel.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        QCOMPARE(singleModel.selectedCount(), 2);

        HandListModel pairModel;
        QVERIFY(pairModel.setCards(cards));
        QVERIFY(pairModel.selectSingle(0));
        QVERIFY(pairModel.selectSingle(1));
        QVERIFY(pairModel.selectSingle(4));
        QVERIFY(pairModel.selectSingle(5));
        const auto pattern = pairModel.completeEndpointSelection(PLAYER_COUNT, true);
        QVERIFY(pattern.has_value());
        QCOMPARE(pattern->type, CardPatternType::ConsecutivePairs);
        QCOMPARE(pairModel.selectedCount(), 6);
    }

    void testCompletionChangesOnlySelectionState() {
        // I01 补牌前后手牌行数、每张实体与总数都不变
        const auto cards = sequenceOfHand(Rank::Three, 5, 1);
        HandListModel model;
        QVERIFY(model.setCards(cards));
        std::vector<CardId> idsBefore;
        for (int row = 0; row < model.rowCount(); ++row) {
            idsBefore.push_back(model.cardAt(row).id());
        }
        const int rowsBefore = model.rowCount();
        QVERIFY(model.selectSingle(0));
        QVERIFY(model.selectSingle(4));
        QVERIFY(model.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        QCOMPARE(model.rowCount(), rowsBefore);
        std::vector<CardId> idsAfter;
        for (int row = 0; row < model.rowCount(); ++row) {
            idsAfter.push_back(model.cardAt(row).id());
        }
        QVERIFY(idsAfter == idsBefore);
    }

    void testCompletionKeepsSingleSelectionSpeechRoles() {
        // I02 端点保留单张朗读标记，自动补入的中间牌按整组朗读
        const auto cards = sequenceOfHand(Rank::Three, 5, 1);
        HandListModel model;
        QVERIFY(model.setCards(cards));
        QVERIFY(model.selectSingle(0));
        QCOMPARE(model.data(model.index(0, 0), Qt::AccessibleTextRole).toString(),
                 QStringLiteral("3"));
        QVERIFY(model.selectSingle(4));
        QVERIFY(model.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        QCOMPARE(model.data(model.index(0, 0), Qt::AccessibleTextRole).toString(),
                 QStringLiteral("3"));
        QCOMPARE(model.data(model.index(4, 0), Qt::AccessibleTextRole).toString(),
                 QStringLiteral("7"));
        QCOMPARE(model.data(model.index(1, 0), Qt::AccessibleTextRole).toString(),
                 QString::fromUtf8(u8"1张4"));
        QCOMPARE(model.data(model.index(3, 0), Qt::AccessibleTextRole).toString(),
                 QString::fromUtf8(u8"1张6"));
    }

    void testCompletionAppendsAddedCardsAfterManualPicks() {
        // I03 手动端点在前，自动补入按行号在后
        const auto cards = sequenceOfHand(Rank::Three, 5, 1);
        HandListModel model;
        QVERIFY(model.setCards(cards));
        QVERIFY(model.selectSingle(4));
        QVERIFY(model.selectSingle(0));
        QVERIFY(model.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        std::vector<Rank> order;
        while (const auto card = model.deselectNextPickedCard()) {
            order.push_back(card->rank());
        }
        const std::vector<Rank> expected = {Rank::Seven, Rank::Three, Rank::Four,
                                           Rank::Five, Rank::Six};
        QVERIFY(order == expected);
    }

    void testClearAndNewCardsDropPreviousCompletionState() {
        // I04 清空与换牌组不残留上次补牌状态
        const auto cards = sequenceOfHand(Rank::Three, 5, 1);
        HandListModel model;
        QVERIFY(model.setCards(cards));
        QVERIFY(model.selectSingle(0));
        QVERIFY(model.selectSingle(4));
        QVERIFY(model.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        model.clearSelection();
        QCOMPARE(model.selectedCount(), 0);
        QVERIFY(!model.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        QVERIFY(model.selectSingle(0));
        QVERIFY(model.selectSingle(4));
        QVERIFY(model.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        QCOMPARE(model.selectedCount(), 5);

        HandListModel replaced;
        QVERIFY(replaced.setCards(cards));
        QVERIFY(replaced.selectSingle(0));
        QVERIFY(replaced.selectSingle(4));
        QVERIFY(replaced.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        QVERIFY(replaced.setCards(sequenceOfHand(Rank::Seven, 5, 1)));
        QCOMPARE(replaced.selectedCount(), 0);
        QVERIFY(replaced.selectedCardIds().empty());
    }

    void testCompletionEmitsSingleFinalDataChanged() {
        // I06 成功补牌只发一次最终选择变化信号，收信号时已是完整目标集合
        const auto cards = sequenceOfHand(Rank::Three, 5, 1);
        HandListModel model;
        QVERIFY(model.setCards(cards));
        QVERIFY(model.selectSingle(0));
        QVERIFY(model.selectSingle(4));
        int changes = 0;
        int countAtSignal = -1;
        int topRow = -1;
        int bottomRow = -1;
        QObject::connect(&model, &QAbstractItemModel::dataChanged,
            [&](const QModelIndex& topLeft, const QModelIndex& bottomRight,
                const QVector<int>& roles) {
                ++changes;
                countAtSignal = model.selectedCount();
                topRow = topLeft.row();
                bottomRow = bottomRight.row();
                QCOMPARE(roles, QVector<int>{static_cast<int>(HandListModel::SelectedRole)});
            });
        QVERIFY(model.completeEndpointSelection(PLAYER_COUNT, true).has_value());
        QCOMPARE(changes, 1);
        QCOMPARE(countAtSignal, 5);
        QCOMPARE(topRow, 1);
        QCOMPARE(bottomRow, 3);
    }

    // ===== 首尾选牌辅助：真实 Qt / Windows 交互用例（U 系列）=====

    void testQtTakeEndpointCompletesStraightWithSingleAnnouncement() {
        // U01 真实键盘拿起 3 与 7 自动补齐；U03 只有一次范围公告与一次拿牌动作；U10 引擎状态不变
        QTemporaryDir traceDir;
        QVERIFY(traceDir.isValid());
        DiagnosticTraceService trace;
        trace.init(traceDir.path());

        GameEngine engine;
        preparePlayingHand(engine, sequenceOfHand(Rank::Three, 5, 1));
        auto& fullState = engine.state().fullState();

        AccessibilityService accessibility;
        MainWindow window(engine, accessibility, &trace);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.refreshFromState();

        auto* handView = window.findChild<QListView*>();
        QVERIFY(handView);
        auto* handModel = qobject_cast<HandListModel*>(handView->model());
        QVERIFY(handModel);
        QCOMPARE(handModel->rowCount(), 5);
        auto* statusLabel = window.statusBar()->findChild<QLabel*>();
        QVERIFY(statusLabel);

        const size_t handSizeBefore = fullState.players[0].hand.size();
        const PlayerId currentBefore = fullState.currentPlayer;
        const uint64_t sequenceBefore = fullState.eventSequence;
        const size_t lastPlayedBefore = fullState.lastPlayedCards.size();

        QTest::keyClick(&window, Qt::Key_Home);
        QTest::keyClick(&window, Qt::Key_Up);
        QCOMPARE(handModel->selectedCount(), 1);
        QCOMPARE(statusLabel->accessibleName(), QStringLiteral("3"));

        QTest::keyClick(&window, Qt::Key_End);
        QCOMPARE(handView->currentIndex().row(), 4);
        // 浏览键本身也会产生一次公告，因此在真正补牌的动作之前取基线。
        trace.flush();
        const int speechBefore = traceCount(trace, QStringLiteral("speech_delivery"));
        const int actionsBefore = traceCount(trace, QStringLiteral("hand_action"));
        QTest::keyClick(&window, Qt::Key_Up);
        trace.flush();

        QCOMPARE(handModel->selectedCount(), 5);
        for (int row = 0; row < 5; ++row) QVERIFY(handModel->isSelected(row));
        QCOMPARE(statusLabel->accessibleName(), QString::fromUtf8(u8"3到7顺子"));

        QCOMPARE(traceCount(trace, QStringLiteral("speech_delivery")) - speechBefore, 1);
        QCOMPARE(traceCount(trace, QStringLiteral("hand_action")) - actionsBefore, 1);

        QCOMPARE(fullState.players[0].hand.size(), handSizeBefore);
        QCOMPARE(fullState.currentPlayer, currentBefore);
        QCOMPARE(fullState.eventSequence, sequenceBefore);
        QCOMPARE(fullState.lastPlayedCards.size(), lastPlayedBefore);

        auto* table = window.findChild<CardTableWidget*>(QStringLiteral("visualCardTable"));
        QVERIFY(table);
        QCOMPARE(table->displayedSelectedCardCount(), 5);

        engine.state().setPhase(GamePhase::NotStarted);
        window.close();
    }

    void testQtTakeEndpointCompletesStraightInReverseDirection() {
        // U02 先 7 后 3 同样成立，焦点留在第二端点，浏览规则不变
        GameEngine engine;
        preparePlayingHand(engine, sequenceOfHand(Rank::Three, 7, 1));
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.refreshFromState();

        auto* handView = window.findChild<QListView*>();
        QVERIFY(handView);
        auto* handModel = qobject_cast<HandListModel*>(handView->model());
        QVERIFY(handModel);
        QCOMPARE(handModel->rowCount(), 7);
        auto* statusLabel = window.statusBar()->findChild<QLabel*>();
        QVERIFY(statusLabel);

        QTest::keyClick(&window, Qt::Key_End);
        QCOMPARE(handView->currentIndex().row(), 6);
        QTest::keyClick(&window, Qt::Key_Left, Qt::ShiftModifier);
        QTest::keyClick(&window, Qt::Key_Left, Qt::ShiftModifier);
        QCOMPARE(handView->currentIndex().row(), 4);
        QCOMPARE(handModel->cardAt(handView->currentIndex().row()).rank(), Rank::Seven);
        QTest::keyClick(&window, Qt::Key_Up);
        QCOMPARE(handModel->selectedCount(), 1);
        QTest::keyClick(&window, Qt::Key_Home);
        QCOMPARE(handView->currentIndex().row(), 0);
        QTest::keyClick(&window, Qt::Key_Up);
        QCOMPARE(handModel->selectedCount(), 5);
        QCOMPARE(handView->currentIndex().row(), 0);
        QCOMPARE(statusLabel->accessibleName(), QString::fromUtf8(u8"3到7顺子"));
        QVERIFY(!handModel->isSelected(5));
        QVERIFY(!handModel->isSelected(6));

        QTest::keyClick(&window, Qt::Key_Right);
        QCOMPARE(handView->currentIndex().row(), 5);
        QVERIFY(!handModel->isSelected(handView->currentIndex().row()));
        QTest::keyClick(&window, Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(handView->currentIndex().row(), 6);
        QVERIFY(!handModel->isSelected(handView->currentIndex().row()));

        engine.state().setPhase(GamePhase::NotStarted);
        window.close();
    }

    void testIncompleteEndpointKeepsSingleCardSpeech() {
        // U04 缺 6 时第二张 7 仍按单张朗读，选择保持两张
        GameEngine engine;
        preparePlayingHand(engine,
            handOfCounts({{Rank::Three, 1}, {Rank::Four, 1}, {Rank::Five, 1}, {Rank::Seven, 1}}));
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.refreshFromState();

        auto* handView = window.findChild<QListView*>();
        QVERIFY(handView);
        auto* handModel = qobject_cast<HandListModel*>(handView->model());
        QVERIFY(handModel);
        QCOMPARE(handModel->rowCount(), 4);
        auto* statusLabel = window.statusBar()->findChild<QLabel*>();
        QVERIFY(statusLabel);

        QTest::keyClick(&window, Qt::Key_Home);
        QTest::keyClick(&window, Qt::Key_Up);
        QCOMPARE(handModel->selectedCount(), 1);
        QCOMPARE(statusLabel->accessibleName(), QStringLiteral("3"));
        QTest::keyClick(&window, Qt::Key_End);
        QCOMPARE(handView->currentIndex().row(), 3);
        QTest::keyClick(&window, Qt::Key_Up);
        QCOMPARE(handModel->selectedCount(), 2);
        QCOMPARE(statusLabel->accessibleName(), QStringLiteral("7"));
        QVERIFY(handModel->isSelected(0));
        QVERIFY(handModel->isSelected(3));

        engine.state().setPhase(GamePhase::NotStarted);
        window.close();
    }

    void testCompletionKeepsRemainingSameRankCardsBrowsable() {
        // U05 同点多张时剩余牌仍可正常浏览
        GameEngine engine;
        preparePlayingHand(engine,
            handOfCounts({{Rank::Three, 2}, {Rank::Four, 1}, {Rank::Five, 1},
                          {Rank::Six, 1}, {Rank::Seven, 2}}));
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.refreshFromState();

        auto* handView = window.findChild<QListView*>();
        QVERIFY(handView);
        auto* handModel = qobject_cast<HandListModel*>(handView->model());
        QVERIFY(handModel);
        QCOMPARE(handModel->rowCount(), 7);

        QTest::keyClick(&window, Qt::Key_Home);
        QTest::keyClick(&window, Qt::Key_Up);
        QTest::keyClick(&window, Qt::Key_End);
        QTest::keyClick(&window, Qt::Key_Up);
        QCOMPARE(handModel->selectedCount(), 5);
        QCOMPARE(handModel->unselectedCountOfRank(Rank::Three), 1);
        QCOMPARE(handModel->unselectedCountOfRank(Rank::Seven), 1);

        QTest::keyClick(&window, Qt::Key_Left);
        QCOMPARE(handView->currentIndex().row(), 1);
        QVERIFY(!handModel->isSelected(handView->currentIndex().row()));
        QTest::keyClick(&window, Qt::Key_End);
        QCOMPARE(handView->currentIndex().row(), 6);
        QVERIFY(!handModel->isSelected(handView->currentIndex().row()));

        engine.state().setPhase(GamePhase::NotStarted);
        window.close();
    }

    void testGroupEntryCompletesConsecutivePairs() {
        // U06 两次 Ctrl+上 拿完整对子组使选择成为 2+2 时补连对
        GameEngine engine;
        preparePlayingHand(engine,
            handOfCounts({{Rank::Four, 2}, {Rank::Five, 2}, {Rank::Six, 2}, {Rank::Nine, 3}}));
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.refreshFromState();

        auto* handView = window.findChild<QListView*>();
        QVERIFY(handView);
        auto* handModel = qobject_cast<HandListModel*>(handView->model());
        QVERIFY(handModel);
        QCOMPARE(handModel->rowCount(), 9);
        auto* statusLabel = window.statusBar()->findChild<QLabel*>();
        QVERIFY(statusLabel);

        QTest::keyClick(&window, Qt::Key_Home);
        QTest::keyClick(&window, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 2);
        QCOMPARE(statusLabel->accessibleName(), QString::fromUtf8(u8"对4"));
        QTest::keyClick(&window, Qt::Key_Right);
        QTest::keyClick(&window, Qt::Key_Right);
        QCOMPARE(handView->currentIndex().row(), 4);
        QTest::keyClick(&window, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 6);
        QCOMPARE(statusLabel->accessibleName(), QString::fromUtf8(u8"4到6连对"));
        QVERIFY(handModel->isSelected(0));
        QVERIFY(handModel->isSelected(1));
        QVERIFY(handModel->isSelected(2));
        QVERIFY(handModel->isSelected(3));
        QVERIFY(handModel->isSelected(4));
        QVERIFY(handModel->isSelected(5));
        QVERIFY(!handModel->isSelected(6));

        QTest::keyClick(&window, Qt::Key_Down, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 0);
        QCOMPARE(statusLabel->accessibleName(), QString::fromUtf8(u8"4到6连对"));

        engine.state().setPhase(GamePhase::NotStarted);
        window.close();
    }

    void testGroupEntryKeepsTripleUnchanged() {
        // U06 续：点数组三张时保留三张，不伪造 2+2
        GameEngine engine;
        preparePlayingHand(engine, handOfCounts({{Rank::Three, 3}, {Rank::Six, 2}}));
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.refreshFromState();

        auto* handView = window.findChild<QListView*>();
        QVERIFY(handView);
        auto* handModel = qobject_cast<HandListModel*>(handView->model());
        QVERIFY(handModel);
        QCOMPARE(handModel->rowCount(), 5);
        auto* statusLabel = window.statusBar()->findChild<QLabel*>();
        QVERIFY(statusLabel);

        QTest::keyClick(&window, Qt::Key_Home);
        QTest::keyClick(&window, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 3);
        QCOMPARE(statusLabel->accessibleName(), QString::fromUtf8(u8"3张3"));
        QTest::keyClick(&window, Qt::Key_Right);
        QCOMPARE(handView->currentIndex().row(), 3);
        QTest::keyClick(&window, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 5);
        QCOMPARE(statusLabel->accessibleName(), QString::fromUtf8(u8"对6"));
        QVERIFY(handModel->isSelected(0));
        QVERIFY(handModel->isSelected(1));
        QVERIFY(handModel->isSelected(2));

        engine.state().setPhase(GamePhase::NotStarted);
        window.close();
    }

    void testPutDownAfterCompletionReleasesOneAndIsNotRefilled() {
        // U07 下键一次只放下一张；焦点已选先放焦点牌，否则按拿起顺序
        GameEngine engine;
        preparePlayingHand(engine, sequenceOfHand(Rank::Three, 5, 1));
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
        QTest::keyClick(&window, Qt::Key_Up);
        QTest::keyClick(&window, Qt::Key_End);
        QTest::keyClick(&window, Qt::Key_Up);
        QCOMPARE(handModel->selectedCount(), 5);

        QTest::keyClick(&window, Qt::Key_Down);
        QCOMPARE(handModel->selectedCount(), 4);
        QVERIFY(!handModel->isSelected(4));
        QTest::keyClick(&window, Qt::Key_Down);
        QCOMPARE(handModel->selectedCount(), 3);
        QVERIFY(!handModel->isSelected(0));
        QVERIFY(handModel->isSelected(1));
        QVERIFY(handModel->isSelected(3));

        window.refreshFromState();
        QCOMPARE(handModel->selectedCount(), 3);
        QVERIFY(!handModel->isSelected(0));
        QVERIFY(!handModel->isSelected(4));

        engine.state().setPhase(GamePhase::NotStarted);
        window.close();
    }

    void testControlDownAnnouncesRangeForCompletedStraight() {
        // U08 Ctrl+下全部放下时用放下前完整连牌范围朗读
        GameEngine engine;
        preparePlayingHand(engine, sequenceOfHand(Rank::Three, 5, 1));
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.refreshFromState();

        auto* handView = window.findChild<QListView*>();
        QVERIFY(handView);
        auto* handModel = qobject_cast<HandListModel*>(handView->model());
        QVERIFY(handModel);
        auto* statusLabel = window.statusBar()->findChild<QLabel*>();
        QVERIFY(statusLabel);

        QTest::keyClick(&window, Qt::Key_Home);
        QTest::keyClick(&window, Qt::Key_Up);
        QTest::keyClick(&window, Qt::Key_End);
        QTest::keyClick(&window, Qt::Key_Up);
        QCOMPARE(handModel->selectedCount(), 5);
        QTest::keyClick(&window, Qt::Key_Down, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 0);
        QCOMPARE(statusLabel->accessibleName(), QString::fromUtf8(u8"3到7顺子"));

        auto* table = window.findChild<CardTableWidget*>(QStringLiteral("visualCardTable"));
        QVERIFY(table);
        QCOMPARE(table->displayedSelectedCardCount(), 0);

        engine.state().setPhase(GamePhase::NotStarted);
        window.close();
    }

    void testControlDownKeepsGroupTextForMixedSelection() {
        // U09 不构成牌型的混合选择仍然保留旧组文案
        GameEngine engine;
        preparePlayingHand(engine, handOfCounts({{Rank::Three, 3}, {Rank::Six, 2}}));
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.refreshFromState();

        auto* handView = window.findChild<QListView*>();
        QVERIFY(handView);
        auto* handModel = qobject_cast<HandListModel*>(handView->model());
        QVERIFY(handModel);
        auto* statusLabel = window.statusBar()->findChild<QLabel*>();
        QVERIFY(statusLabel);

        QTest::keyClick(&window, Qt::Key_Home);
        QTest::keyClick(&window, Qt::Key_Up, Qt::ControlModifier);
        QTest::keyClick(&window, Qt::Key_Right);
        QTest::keyClick(&window, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 5);
        QTest::keyClick(&window, Qt::Key_Down, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 0);
        QCOMPARE(statusLabel->accessibleName(), QString::fromUtf8(u8"3张3、对6"));

        engine.state().setPhase(GamePhase::NotStarted);
        window.close();
    }

    void testEnterSubmitsExactlySelectedStraight() {
        // U11 按回车才真正出牌，提交的正是所选实体
        GameEngine engine;
        preparePlayingHand(engine, sequenceOfHand(Rank::Three, 5, 1));
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
        QTest::keyClick(&window, Qt::Key_Up);
        QTest::keyClick(&window, Qt::Key_End);
        QTest::keyClick(&window, Qt::Key_Up);
        QCOMPARE(handModel->selectedCount(), 5);
        auto& fullState = engine.state().fullState();
        QCOMPARE(fullState.players[0].hand.size(), size_t(5));

        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(fullState.players[0].hand.size(), size_t(0));
        QCOMPARE(fullState.lastPlayedCards.size(), size_t(5));
        QCOMPARE(handModel->selectedCount(), 0);
        for (int row = 0; row < handModel->rowCount(); ++row) {
            QVERIFY(!handModel->isSelected(row));
        }

        engine.state().setPhase(GamePhase::NotStarted);
        window.close();
    }

    void testRejectedSelectionKeepsHandAndTurn() {
        // U12 压不过上一手时回车失败，不扣牌、不轮转、规则不改
        GameEngine engine;
        preparePlayingHand(engine, sequenceOfHand(Rank::Three, 5, 1));
        auto& fullState = engine.state().fullState();
        fullState.lastPlayedBy = PlayerId::Player2;
        fullState.lastPlayedCards = handOfCounts({{Rank::Nine, 2}});
        fullState.consecutivePasses = 0;
        fullState.eventSequence = 0;

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
        QTest::keyClick(&window, Qt::Key_Up);
        QTest::keyClick(&window, Qt::Key_End);
        QTest::keyClick(&window, Qt::Key_Up);
        QCOMPARE(handModel->selectedCount(), 5);
        const size_t handSizeBefore = fullState.players[0].hand.size();
        const PlayerId currentBefore = fullState.currentPlayer;
        const size_t lastPlayedBefore = fullState.lastPlayedCards.size();

        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(fullState.players[0].hand.size(), handSizeBefore);
        QCOMPARE(fullState.currentPlayer, currentBefore);
        QCOMPARE(fullState.lastPlayedCards.size(), lastPlayedBefore);
        QCOMPARE(handModel->selectedCount(), 5);
        QCOMPARE(fullState.eventSequence, uint64_t(0));

        QTest::keyClick(&window, Qt::Key_Down, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 0);
        engine.state().setPhase(GamePhase::NotStarted);
        window.close();
    }

    void testPhasesWithoutPlayingDoNotTakeCards() {
        // U13 电脑回合仍可预选补牌；其它阶段键盘不拿牌、不补牌
        GameEngine engine;
        preparePlayingHand(engine, sequenceOfHand(Rank::Three, 5, 1));
        engine.state().fullState().currentPlayer = PlayerId::Player2;
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
        QTest::keyClick(&window, Qt::Key_Up);
        QTest::keyClick(&window, Qt::Key_End);
        QTest::keyClick(&window, Qt::Key_Up);
        QCOMPARE(handModel->selectedCount(), 5);

        for (const auto phase : {GamePhase::Bidding, GamePhase::Paused,
                                 GamePhase::Finished, GamePhase::NotStarted}) {
            engine.state().setPhase(phase);
            QTest::keyClick(&window, Qt::Key_Home);
            QTest::keyClick(&window, Qt::Key_Up);
            QCOMPARE(handModel->selectedCount(), 5);
            QTest::keyClick(&window, Qt::Key_Up, Qt::ControlModifier);
            QCOMPARE(handModel->selectedCount(), 5);
        }

        engine.state().setPhase(GamePhase::NotStarted);
        window.close();
    }

    void testAutoRepeatKeyDoesNotTakeExtraCards() {
        // U14 长按自动重复不会额外拿牌
        GameEngine engine;
        preparePlayingHand(engine, sequenceOfHand(Rank::Three, 5, 1));
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
        QKeyEvent repeatPress(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier,
                              QString(), true);
        QApplication::sendEvent(&window, &repeatPress);
        QCOMPARE(handModel->selectedCount(), 0);

        QTest::keyClick(&window, Qt::Key_Up);
        QCOMPARE(handModel->selectedCount(), 1);
        QTest::keyClick(&window, Qt::Key_End);
        QTest::keyClick(&window, Qt::Key_Up);
        QCOMPARE(handModel->selectedCount(), 5);

        engine.state().setPhase(GamePhase::NotStarted);
        window.close();
    }

    void testCustomizedTakeKeysTriggerEndpointCompletion() {
        // U15 自定义“拿起一张/拿起整组”的键同样触发首尾补牌，旧键按自定义规则失效
        AppSettings custom;
        custom.shortcuts.setBinding(ShortcutAction::PickCard, {Qt::Key_PageUp, Qt::NoModifier});
        custom.shortcuts.setBinding(ShortcutAction::PickRankGroup,
                                    {Qt::Key_PageUp, Qt::ControlModifier});
        custom.shortcuts.setBinding(ShortcutAction::FirstRankGroup,
                                    {Qt::Key_PageDown, Qt::NoModifier});
        custom.shortcuts.setBinding(ShortcutAction::LastRankGroup,
                                    {Qt::Key_PageDown, Qt::ControlModifier});
        custom.normalize();
        SettingsRepository repository;
        repository.setData(custom.toJson());
        DataPaths::ensureDirectories();
        QVERIFY(repository.save(DataPaths::settingsFile()));

        GameEngine engine;
        preparePlayingHand(engine, sequenceOfHand(Rank::Three, 5, 1));
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.refreshFromState();

        auto* handView = window.findChild<QListView*>();
        QVERIFY(handView);
        auto* handModel = qobject_cast<HandListModel*>(handView->model());
        QVERIFY(handModel);
        auto* statusLabel = window.statusBar()->findChild<QLabel*>();
        QVERIFY(statusLabel);

        QTest::keyClick(&window, Qt::Key_PageDown);
        QCOMPARE(handView->currentIndex().row(), 0);
        QTest::keyClick(&window, Qt::Key_PageUp);
        QCOMPARE(handModel->selectedCount(), 1);
        QTest::keyClick(&window, Qt::Key_PageDown, Qt::ControlModifier);
        QCOMPARE(handView->currentIndex().row(), 4);
        QTest::keyClick(&window, Qt::Key_PageUp);
        QCOMPARE(handModel->selectedCount(), 5);
        QCOMPARE(statusLabel->accessibleName(), QString::fromUtf8(u8"3到7顺子"));

        AppSettings defaults;
        defaults.normalize();
        repository.setData(defaults.toJson());
        QVERIFY(repository.save(DataPaths::settingsFile()));

        engine.state().setPhase(GamePhase::NotStarted);
        window.close();
    }

    void testNativeHookMessageCompletesStraight() {
        // U16 Windows 原生消息分发入口与 Qt 事件结果一致
#ifdef Q_OS_WIN
        GameEngine engine;
        preparePlayingHand(engine, sequenceOfHand(Rank::Three, 5, 1));
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.refreshFromState();

        auto* handView = window.findChild<QListView*>();
        QVERIFY(handView);
        auto* handModel = qobject_cast<HandListModel*>(handView->model());
        QVERIFY(handModel);
        auto* statusLabel = window.statusBar()->findChild<QLabel*>();
        QVERIFY(statusLabel);

        constexpr UINT keyboardHookMessage = WM_APP + 0x4F;
        const HWND windowHandle = reinterpret_cast<HWND>(window.winId());
        QVERIFY(PostMessageW(windowHandle, keyboardHookMessage, VK_HOME, 0));
        QTRY_COMPARE(handView->currentIndex().row(), 0);
        QVERIFY(PostMessageW(windowHandle, keyboardHookMessage, VK_UP, 0));
        QTRY_COMPARE(handModel->selectedCount(), 1);
        QVERIFY(PostMessageW(windowHandle, keyboardHookMessage, VK_END, 0));
        QTRY_COMPARE(handView->currentIndex().row(), 4);
        QVERIFY(PostMessageW(windowHandle, keyboardHookMessage, VK_UP, 0));
        QTRY_COMPARE(handModel->selectedCount(), 5);
        QTRY_COMPARE(statusLabel->accessibleName(), QString::fromUtf8(u8"3到7顺子"));

        engine.state().setPhase(GamePhase::NotStarted);
        window.close();
#else
        QSKIP("Windows native keyboard hook path only");
#endif
    }

    void testRangeAnnouncementKeepsAccessibleChannelClean() {
        // U18 新范围公告仍走既有公告通道，不引入花色或“选中”噪声
        GameEngine engine;
        preparePlayingHand(engine, sequenceOfHand(Rank::Three, 5, 1));
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.refreshFromState();

        auto* handView = window.findChild<QListView*>();
        QVERIFY(handView);
        auto* handModel = qobject_cast<HandListModel*>(handView->model());
        QVERIFY(handModel);
        auto* statusLabel = window.statusBar()->findChild<QLabel*>();
        QVERIFY(statusLabel);

        QTest::keyClick(&window, Qt::Key_Home);
        QTest::keyClick(&window, Qt::Key_Up);
        QTest::keyClick(&window, Qt::Key_End);
        QTest::keyClick(&window, Qt::Key_Up);
        QCOMPARE(statusLabel->accessibleName(), QString::fromUtf8(u8"3到7顺子"));
        QVERIFY(!containsForbiddenSpeech(statusLabel->accessibleName()));
        QCOMPARE(statusLabel->accessibleName().count(QString::fromUtf8(u8"3到7顺子")), 1);

        // 浏览键恢复无障碍文本，且端点仍按单张朗读
        QTest::keyClick(&window, Qt::Key_Home);
        QCOMPARE(handModel->data(handModel->index(0, 0), Qt::AccessibleTextRole).toString(),
                 QStringLiteral("3"));
        QCOMPARE(handModel->data(handModel->index(4, 0), Qt::AccessibleTextRole).toString(),
                 QStringLiteral("7"));
        QVERIFY(!containsForbiddenSpeech(
            handModel->data(handModel->index(0, 0), Qt::AccessibleTextRole).toString()));

        engine.state().setPhase(GamePhase::NotStarted);
        window.close();
    }

    void testLastActionRepeatUsesRangeForSequences() {
        // U19/U20 上一手复读与最近公告复读使用范围文字，只出现一次
        GameEngine engine;
        preparePlayingHand(engine, sequenceOfHand(Rank::Three, 5, 1));
        auto& fullState = engine.state().fullState();
        fullState.lastPlayedBy = PlayerId::Player1;
        fullState.lastPlayedCards = sequenceOfHand(Rank::Three, 5, 1);
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto* statusLabel = window.statusBar()->findChild<QLabel*>();
        QVERIFY(statusLabel);

        QTest::keyClick(&window, Qt::Key_F12);
        QCOMPARE(statusLabel->text(), QString::fromUtf8(u8"地主，3到7顺子"));
        QCOMPARE(statusLabel->text().count(QString::fromUtf8(u8"3到7顺子")), 1);

        fullState.lastPlayedCards = sequenceOfHand(Rank::Four, 3, 2);
        QTest::keyClick(&window, Qt::Key_F12);
        QCOMPARE(statusLabel->text(), QString::fromUtf8(u8"地主，4到6连对"));

        fullState.lastPlayedCards = sequenceOfHand(Rank::Three, 4, 3);
        QTest::keyClick(&window, Qt::Key_F12);
        QCOMPARE(statusLabel->text(), QString::fromUtf8(u8"地主，3到6飞机"));

        fullState.lastPlayedBy = PlayerId::Player2;
        QTest::keyClick(&window, Qt::Key_F12);
        QVERIFY(!statusLabel->text().startsWith(QString::fromUtf8(u8"地主，")));
        QVERIFY(statusLabel->text().endsWith(QString::fromUtf8(u8"3到6飞机")));

        engine.state().setPhase(GamePhase::NotStarted);
        window.close();
    }

    void testHintAndNewGameDoNotTriggerCompletion() {
        // U22 提示、刷新与新局都不会意外补牌
        GameEngine engine;
        preparePlayingHand(engine, sequenceOfHand(Rank::Three, 5, 1));
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.refreshFromState();

        auto* handView = window.findChild<QListView*>();
        QVERIFY(handView);
        auto* handModel = qobject_cast<HandListModel*>(handView->model());
        QVERIFY(handModel);
        auto* statusLabel = window.statusBar()->findChild<QLabel*>();
        QVERIFY(statusLabel);

        QTest::keyClick(&window, Qt::Key_Home);
        QTest::keyClick(&window, Qt::Key_Up);
        QTest::keyClick(&window, Qt::Key_End);
        QTest::keyClick(&window, Qt::Key_Up);
        QCOMPARE(handModel->selectedCount(), 5);
        QTest::keyClick(&window, Qt::Key_Down);
        QCOMPARE(handModel->selectedCount(), 4);
        window.refreshFromState();
        QCOMPARE(handModel->selectedCount(), 4);
        QVERIFY(!handModel->isSelected(4));

        QPushButton* hintButton = nullptr;
        for (auto* button : window.findChildren<QPushButton*>()) {
            if (button->accessibleName() == QString::fromUtf8(u8"出牌提示")) {
                hintButton = button;
                break;
            }
        }
        QVERIFY(hintButton);
        QVERIFY(hintButton->isEnabled());
        hintButton->click();
        const auto match = QRegularExpression(QString::fromUtf8(u8"共(\\d+)张"))
                               .match(statusLabel->text());
        QVERIFY(match.hasMatch());
        QCOMPARE(handModel->selectedCount(), match.captured(1).toInt());

        engine.state().setPhase(GamePhase::NotStarted);
        window.startNewGame();
        QVERIFY(handModel->rowCount() > 0);
        QCOMPARE(handModel->selectedCount(), 0);

        engine.state().setPhase(GamePhase::NotStarted);
        window.close();
    }

    void testManualTripleGroupsAnnounceAirplaneRange() {
        // U23 逐组手工拿齐三张组：333444 报 3到4飞机，拿齐到 6 才报 3到6飞机
        GameEngine engine;
        preparePlayingHand(engine, sequenceOfHand(Rank::Three, 4, 3));
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.refreshFromState();

        auto* handView = window.findChild<QListView*>();
        QVERIFY(handView);
        auto* handModel = qobject_cast<HandListModel*>(handView->model());
        QVERIFY(handModel);
        QCOMPARE(handModel->rowCount(), 12);
        auto* statusLabel = window.statusBar()->findChild<QLabel*>();
        QVERIFY(statusLabel);

        QTest::keyClick(&window, Qt::Key_Home);
        QTest::keyClick(&window, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 3);
        QCOMPARE(statusLabel->accessibleName(), QString::fromUtf8(u8"3张3"));
        QTest::keyClick(&window, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 6);
        QCOMPARE(statusLabel->accessibleName(), QString::fromUtf8(u8"3到4飞机"));
        QTest::keyClick(&window, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 9);
        QCOMPARE(statusLabel->accessibleName(), QString::fromUtf8(u8"3到5飞机"));
        QTest::keyClick(&window, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 12);
        QCOMPARE(statusLabel->accessibleName(), QString::fromUtf8(u8"3到6飞机"));
        QTest::keyClick(&window, Qt::Key_Down, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 0);
        QCOMPARE(statusLabel->accessibleName(), QString::fromUtf8(u8"3到6飞机"));

        engine.state().setPhase(GamePhase::NotStarted);
        window.close();
    }

    void testTripleEndpointsDoNotAutoCompleteMiddleTriples() {
        // U23 续：首尾各三张不会自动补中间三张组
        GameEngine engine;
        preparePlayingHand(engine, sequenceOfHand(Rank::Three, 4, 3));
        AccessibilityService accessibility;
        MainWindow window(engine, accessibility);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.refreshFromState();

        auto* handView = window.findChild<QListView*>();
        QVERIFY(handView);
        auto* handModel = qobject_cast<HandListModel*>(handView->model());
        QVERIFY(handModel);
        auto* statusLabel = window.statusBar()->findChild<QLabel*>();
        QVERIFY(statusLabel);

        QTest::keyClick(&window, Qt::Key_Home);
        QTest::keyClick(&window, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 3);
        QTest::keyClick(&window, Qt::Key_End);
        QCOMPARE(handView->currentIndex().row(), 9);
        QTest::keyClick(&window, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(handModel->selectedCount(), 6);
        QCOMPARE(statusLabel->accessibleName(), QString::fromUtf8(u8"3张6"));
        for (int row = 3; row < 9; ++row) QVERIFY(!handModel->isSelected(row));

        engine.state().setPhase(GamePhase::NotStarted);
        window.close();
    }

    void testSequenceSoundPlanKeepsEndpointWaveFiles() {
        // U21/U24 出牌音效仍使用首尾 WAV 计划，纯飞机仍是 linkThree.wav
        const auto airplane = sequenceOfHand(Rank::Three, 4, 3);
        GameEvent event;
        event.type = GameEventType::CardsPlayed;
        event.playerId = PlayerId::Player1;
        event.cards = airplane;
        event.pattern = PatternAnalyzer::analyze(airplane, PLAYER_COUNT);
        QCOMPARE(event.pattern.type, CardPatternType::Airplane);
        const auto airplanePlan = buildCardPatternSoundPlan(event);
        QStringList expectedAirplane;
        expectedAirplane << QStringLiteral("card_four/boy/3.wav")
                         << QStringLiteral("card_four/boy/zhi.wav")
                         << QStringLiteral("card_four/boy/6.wav")
                         << QStringLiteral("card_four/boy/linkThree.wav");
        QCOMPARE(asQStringList(airplanePlan.voiceFiles), expectedAirplane);
        QVERIFY(airplanePlan.effectFile.empty());

        const auto straight = sequenceOfHand(Rank::Three, 5, 1);
        event.cards = straight;
        event.pattern = PatternAnalyzer::analyze(straight, PLAYER_COUNT);
        QCOMPARE(event.pattern.type, CardPatternType::Straight);
        const auto straightPlan = buildCardPatternSoundPlan(event);
        QStringList expectedStraight;
        expectedStraight << QStringLiteral("card_four/boy/3.wav")
                         << QStringLiteral("card_four/boy/zhi.wav")
                         << QStringLiteral("card_four/boy/7.wav")
                         << QStringLiteral("card_four/boy/line.wav");
        QCOMPARE(asQStringList(straightPlan.voiceFiles), expectedStraight);
        QCOMPARE(QString::fromStdString(straightPlan.effectFile),
                 QStringLiteral("card_four/shunzi.wav"));

        const auto femalePlan = buildCardPatternSoundPlan(event, true);
        QVERIFY(!femalePlan.voiceFiles.empty());
        QCOMPARE(QString::fromStdString(femalePlan.voiceFiles.front()),
                 QStringLiteral("card_four/girl/3.wav"));

        QStringList requiredFiles = expectedAirplane;
        requiredFiles << QStringLiteral("card_four/shunzi.wav")
                      << QStringLiteral("card_four/girl/3.wav")
                      << QStringLiteral("card_four/girl/6.wav")
                      << QStringLiteral("card_four/girl/zhi.wav")
                      << QStringLiteral("card_four/girl/linkThree.wav");
        for (const QString& file : requiredFiles) {
            const QString path = QDir(QCoreApplication::applicationDirPath())
                .filePath(QStringLiteral("resources/sounds/") + file);
            QString error;
            QVERIFY2(SoundService::validateWaveFile(path, &error),
                     qPrintable(path + error));
        }
    }

private:
    static std::vector<Card> sameRankCards(Rank rank, int count) {

        const Suit suits[] = {Suit::Spades, Suit::Hearts, Suit::Clubs, Suit::Diamonds};
        std::vector<Card> cards;
        for (int index = 0; index < count; ++index) {
            const bool joker = rank == Rank::SmallJoker || rank == Rank::BigJoker;
            cards.push_back(Card::create(rank,
                joker ? Suit::None : suits[index % 4],
                static_cast<DeckIndex>(joker ? 0 : index / 4)));
        }
        return cards;
    }

    static std::vector<Card> handOfCounts(const std::vector<std::pair<Rank, int>>& groups) {
        std::vector<Card> cards;
        for (const auto& group : groups) {
            const auto part = sameRankCards(group.first, group.second);
            cards.insert(cards.end(), part.begin(), part.end());
        }
        return cards;
    }

    static std::vector<Card> sequenceOfHand(Rank start, int length, int copies) {
        std::vector<Card> cards;
        for (int offset = 0; offset < length; ++offset) {
            const auto part = sameRankCards(
                static_cast<Rank>(static_cast<int>(start) + offset), copies);
            cards.insert(cards.end(), part.begin(), part.end());
        }
        return cards;
    }

    static QStringList asQStringList(const std::vector<std::string>& files) {
        QStringList result;
        for (const auto& file : files) result.push_back(QString::fromStdString(file));
        return result;
    }

    static void preparePlayingHand(GameEngine& engine, const std::vector<Card>& cards) {
        auto& state = engine.state();
        auto& fullState = state.fullState();
        state.setPhase(GamePhase::Playing);
        fullState.currentPlayer = PlayerId::Player1;
        fullState.lastPlayedCards.clear();
        fullState.consecutivePasses = 0;
        fullState.players[0].role = Role::Landlord;
        for (size_t index = 1; index < fullState.players.size(); ++index) {
            fullState.players[index].role = Role::Farmer;
        }
        fullState.players[0].hand.clear();
        fullState.players[0].hand.addCards(cards);
        fullState.players[0].hand.sortByRank();
    }

    static int traceCount(DiagnosticTraceService& trace, const QString& type) {
        trace.flush();
        QFile file(trace.traceFilePath());
        if (!file.open(QIODevice::ReadOnly)) return -1;
        int count = 0;
        while (!file.atEnd()) {
            const auto document = QJsonDocument::fromJson(file.readLine());
            if (!document.isObject()) continue;
            if (document.object().value(QStringLiteral("type")).toString() == type) ++count;
        }
        return count;
    }

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
