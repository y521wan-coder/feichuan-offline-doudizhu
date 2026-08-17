#include <QtTest>
#include "app/app_settings.h"
using namespace fpdz;

class TestAppSettings : public QObject {
    Q_OBJECT
private slots:
    void testDefaults() {
        AppSettings settings;
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
        settings.playerNames[0] = QString::fromUtf8(u8"我");
        settings.playerNames[1] = QString::fromUtf8(u8"小张");
        settings.playerNames[2] = QString::fromUtf8(u8"小李");
        settings.playerNames[3] = QString::fromUtf8(u8"小王");

        const AppSettings restored = AppSettings::fromJson(settings.toJson());
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
};

QTEST_MAIN(TestAppSettings)
#include "test_app_settings.moc"
