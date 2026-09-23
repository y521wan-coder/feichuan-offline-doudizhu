#include <QtTest>
#include "app/app_settings.h"
using namespace fpdz;

class TestAppSettings : public QObject {
    Q_OBJECT
private slots:
    void testDefaults() {
        AppSettings settings;
        QCOMPARE(settings.playerCount, PLAYER_COUNT);
        QCOMPARE(settings.autoPassEnabled, true);
        QCOMPARE(settings.autoPassSeconds, 30);
        QCOMPARE(settings.aiDifficulty, static_cast<int>(AiDifficulty::Beginner));
        QCOMPARE(settings.humanVoice, PlayerVoice::Male);
        QCOMPARE(settings.backgroundMusicEnabled, false);
        QCOMPARE(settings.backgroundMusicVolume, 35);
        QCOMPARE(settings.backgroundMusicMode, BackgroundMusicMode::Automatic);
        QCOMPARE(settings.automaticUpdateChecks, true);
        QVERIFY(settings.lastAutomaticUpdateCheckDate.isEmpty());
        QVERIFY(settings.ignoredUpdateVersion.isEmpty());
        QCOMPARE(settings.firstRunGuideShown, false);
        QCOMPARE(settings.firstRunGuideRevision, 0);
        QCOMPARE(settings.shortcuts.binding(ShortcutAction::LastAction).key,
                 static_cast<int>(Qt::Key_F12));
        QCOMPARE(ShortcutSettings::keyName(
                     {Qt::Key_PageDown, Qt::NoModifier}),
                 QString::fromUtf8(u8"下翻页键"));
        QCOMPARE(ShortcutSettings::keyName(
                     {Qt::Key_Return, Qt::ControlModifier}),
                 QString::fromUtf8(u8"Control加回车键"));
        QCOMPARE(ShortcutSettings::keyName(
                     {Qt::Key_Left, Qt::ShiftModifier}),
                 QString::fromUtf8(u8"Shift加左光标键"));
        QVERIFY(!ShortcutSettings::isValidBinding(
            {Qt::Key_Left, Qt::ShiftModifier}));
        QVERIFY(!ShortcutSettings::isValidBinding(
            {Qt::Key_Right, Qt::ShiftModifier}));
        QCOMPARE(ShortcutSettings::keyName(
                     {Qt::Key_F, Qt::AltModifier}),
                 QStringLiteral("Alt加字母F键"));
    }

    void testNormalizeClampsAutoPassSecondsAndDifficulty() {
        AppSettings settings;
        settings.autoPassSeconds = 1;
        settings.aiDifficulty = 9;
        settings.normalize();
        QCOMPARE(settings.autoPassSeconds, 3);
        QCOMPARE(settings.aiDifficulty, static_cast<int>(AiDifficulty::Advanced));

        settings.autoPassSeconds = 999;
        settings.aiDifficulty = -1;
        settings.normalize();
        QCOMPARE(settings.autoPassSeconds, 999);

        settings.autoPassSeconds = 9999;
        settings.normalize();
        QCOMPARE(settings.autoPassSeconds, 1800);
        QCOMPARE(settings.aiDifficulty, 0);
    }

    void testJsonRoundTrip() {
        AppSettings settings;
        settings.playerCount = THREE_PLAYER_COUNT;
        settings.autoPassEnabled = false;
        settings.autoPassSeconds = 120;
        settings.aiDifficulty = static_cast<int>(AiDifficulty::Advanced);
        settings.humanVoice = PlayerVoice::Female;
        settings.backgroundMusicEnabled = true;
        settings.backgroundMusicVolume = 62;
        settings.backgroundMusicMode = BackgroundMusicMode::Intense;
        settings.automaticUpdateChecks = false;
        settings.lastAutomaticUpdateCheckDate = QStringLiteral("2026-07-30");
        settings.ignoredUpdateVersion = QStringLiteral("1.1");
        settings.firstRunGuideShown = true;
        settings.firstRunGuideRevision = 1;
        settings.shortcuts.setBinding(
            ShortcutAction::LastAction, {Qt::Key_PageDown, Qt::NoModifier});
        settings.playerNames[0] = QString::fromUtf8(u8"我");
        settings.playerNames[1] = QString::fromUtf8(u8"小张");
        settings.playerNames[2] = QString::fromUtf8(u8"小李");
        settings.playerNames[3] = QString::fromUtf8(u8"小王");

        const AppSettings restored = AppSettings::fromJson(settings.toJson());
        QCOMPARE(restored.playerCount, THREE_PLAYER_COUNT);
        QCOMPARE(restored.autoPassEnabled, false);
        QCOMPARE(restored.autoPassSeconds, 120);
        QCOMPARE(restored.aiDifficulty, static_cast<int>(AiDifficulty::Advanced));
        QCOMPARE(restored.humanVoice, PlayerVoice::Female);
        QCOMPARE(restored.backgroundMusicEnabled, true);
        QCOMPARE(restored.backgroundMusicVolume, 62);
        QCOMPARE(restored.backgroundMusicMode, BackgroundMusicMode::Intense);
        QCOMPARE(restored.automaticUpdateChecks, false);
        QCOMPARE(restored.lastAutomaticUpdateCheckDate, QStringLiteral("2026-07-30"));
        QCOMPARE(restored.ignoredUpdateVersion, QStringLiteral("1.1"));
        QCOMPARE(restored.firstRunGuideShown, true);
        QCOMPARE(restored.firstRunGuideRevision, 1);
        QCOMPARE(restored.shortcuts.binding(ShortcutAction::LastAction).key,
                 static_cast<int>(Qt::Key_PageDown));
        QCOMPARE(ShortcutSettings::keyName(
                     restored.shortcuts.binding(ShortcutAction::LastAction)),
                 QString::fromUtf8(u8"下翻页键"));
        QCOMPARE(restored.playerNames[0], QString::fromUtf8(u8"我"));
        QCOMPARE(restored.playerNames[1], QString::fromUtf8(u8"小张"));
        QCOMPARE(restored.playerNames[2], QString::fromUtf8(u8"小李"));
        QCOMPARE(restored.playerNames[3], QString::fromUtf8(u8"小王"));
    }

    void testPlayerNamesDefaultAndTrim() {
        AppSettings settings;
        settings.playerNames[0] = QString::fromUtf8(u8"  东家  ");
        settings.playerNames[1] = QString::fromUtf8(u8"   ");
        settings.normalize();

        QCOMPARE(settings.playerNames[0], QString::fromUtf8(u8"东家"));
        QCOMPARE(settings.playerNames[1], QString::fromUtf8(u8"玩家二"));
    }

    void testOutOfRangeDifficultyClamps() {
        QJsonObject json;
        json["aiDifficulty"] = 3;

        const AppSettings restored = AppSettings::fromJson(json);
        QCOMPARE(restored.aiDifficulty, static_cast<int>(AiDifficulty::Advanced));
    }

    void testUnsupportedPlayerCountFallsBackToFour() {
        QJsonObject json;
        json["playerCount"] = 1;
        QCOMPARE(AppSettings::fromJson(json).playerCount, PLAYER_COUNT);

        json["playerCount"] = TWO_PLAYER_COUNT;
        QCOMPARE(AppSettings::fromJson(json).playerCount, TWO_PLAYER_COUNT);

        json["playerCount"] = THREE_PLAYER_COUNT;
        QCOMPARE(AppSettings::fromJson(json).playerCount, THREE_PLAYER_COUNT);
    }

    void testShortcutConflictsAreDetectedAndCorruptJsonFallsBackToDefaults() {
        ShortcutSettings shortcuts;
        const ShortcutBinding pageDown{Qt::Key_PageDown, Qt::NoModifier};
        shortcuts.setBinding(ShortcutAction::LastAction, pageDown);
        const auto conflict = shortcuts.conflictingAction(
            ShortcutAction::BottomCards, pageDown);
        QVERIFY(conflict.has_value());
        QCOMPARE(*conflict, ShortcutAction::LastAction);

        QJsonObject json = shortcuts.toJson();
        json[ShortcutSettings::actionId(ShortcutAction::BottomCards)] =
            json[ShortcutSettings::actionId(ShortcutAction::LastAction)];
        const ShortcutSettings restored = ShortcutSettings::fromJson(json);
        QCOMPARE(restored.binding(ShortcutAction::BottomCards).key,
                 static_cast<int>(Qt::Key_F2));
        QCOMPARE(restored.binding(ShortcutAction::LastAction).key,
                 static_cast<int>(Qt::Key_F12));
    }
};

QTEST_MAIN(TestAppSettings)
#include "test_app_settings.moc"
