#include <QtTest>

#include "app/app_settings.h"
#include "persistence/data_paths.h"
#include "ui/dialogs/sound_manager_dialog.h"
#include "ui/dialogs/settings_dialog.h"
#include "ui/sound_catalog.h"
#include "ui/sound_override_manager.h"
#include "ui/sound_service.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QStandardPaths>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>

using namespace fpdz;

class TestSoundManagement : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        SoundService sound;
        SoundOverrideManager manager(sound);
        QString error;
        QVERIFY2(manager.restoreAll(&error) >= 0, qPrintable(error));
    }

    void cleanupTestCase() {
        SoundService sound;
        SoundOverrideManager manager(sound);
        QString error;
        QVERIFY2(manager.restoreAll(&error) >= 0, qPrintable(error));
    }

    void testCategorySettingsDefaultsAndRoundTrip() {
        AppSettings settings;
        for (std::size_t i = 0; i < SOUND_CATEGORY_COUNT; ++i) {
            const auto category = static_cast<SoundCategory>(i);
            QCOMPARE(settings.isSoundCategoryEnabled(category),
                     category != SoundCategory::BackgroundMusic);
        }
        settings.setSoundCategoryEnabled(SoundCategory::CardPattern, false);
        settings.setSoundCategoryEnabled(SoundCategory::BackgroundMusic, true);
        const AppSettings restored = AppSettings::fromJson(settings.toJson());
        QVERIFY(!restored.isSoundCategoryEnabled(SoundCategory::CardPattern));
        QVERIFY(restored.isSoundCategoryEnabled(SoundCategory::BackgroundMusic));
        QVERIFY(restored.backgroundMusicEnabled);

        QJsonObject legacy;
        legacy[QStringLiteral("backgroundMusicEnabled")] = true;
        const AppSettings legacyRestored = AppSettings::fromJson(legacy);
        QVERIFY(legacyRestored.isSoundCategoryEnabled(SoundCategory::BackgroundMusic));
        QVERIFY(legacyRestored.isSoundCategoryEnabled(SoundCategory::CardPattern));
    }

    void testQueuedFilesReturnImmediateSequenceDuration() {
        SoundService sound;
        QVERIFY(sound.initialize());
        sound.setVolume(26);
        const int duration = sound.playFiles({SoundRequest{
            QStringLiteral("card_four/din.wav"), SoundCategory::YourTurn}});
        QVERIFY(duration > 0);
        {
            std::lock_guard<std::mutex> lock(sound.m_mutex);
            QVERIFY(!sound.m_openHandles.isEmpty());
        }
        sound.stopAll();
    }

    void testPcmVolumeScalingFallback() {
        SoundService sound;

        WAVEFORMATEX format16{};
        format16.wFormatTag = WAVE_FORMAT_PCM;
        format16.nChannels = 1;
        format16.wBitsPerSample = 16;
        QVector<BYTE> pcm16(sizeof(int16_t) * 2);
        auto* samples16 = reinterpret_cast<int16_t*>(pcm16.data());
        samples16[0] = 10000;
        samples16[1] = -10000;
        sound.setVolume(26);
        sound.applyVolume(pcm16, format16);
        QCOMPARE(samples16[0], static_cast<int16_t>(2600));
        QCOMPARE(samples16[1], static_cast<int16_t>(-2600));

        WAVEFORMATEX format8{};
        format8.wFormatTag = WAVE_FORMAT_PCM;
        format8.nChannels = 1;
        format8.wBitsPerSample = 8;
        QVector<BYTE> pcm8{228, 28};
        sound.setVolume(48);
        sound.applyVolume(pcm8, format8);
        QCOMPARE(pcm8[0], static_cast<BYTE>(176));
        QCOMPARE(pcm8[1], static_cast<BYTE>(80));

        sound.setVolume(0);
        QVector<BYTE> silent{228, 28};
        sound.applyVolume(silent, format8);
        QCOMPARE(silent[0], static_cast<BYTE>(128));
        QCOMPARE(silent[1], static_cast<BYTE>(128));
    }

    void testSettingsVolumeArrowsOnlyChangePendingValues() {
        AppSettings settings;
        settings.soundVolume = 26;
        settings.backgroundMusicVolume = 48;
        SettingsDialog dialog(settings);
        auto* effects = dialog.findChild<QSpinBox*>(QStringLiteral("soundVolumeSpinBox"));
        auto* music = dialog.findChild<QSpinBox*>(
            QStringLiteral("backgroundMusicVolumeSpinBox"));
        QVERIFY(effects);
        QVERIFY(music);
        QCOMPARE(effects->singleStep(), 1);
        QCOMPARE(music->singleStep(), 1);

        effects->setFocus();
        QTest::keyClick(effects, Qt::Key_Up);
        QCOMPARE(effects->value(), 27);

        music->setFocus();
        QTest::keyClick(music, Qt::Key_Down);
        QCOMPARE(music->value(), 47);
        QCOMPARE(dialog.settings().soundVolume, 27);
        QCOMPARE(dialog.settings().backgroundMusicVolume, 47);
    }

    void testRapidEffectVolumePreviewDoesNotDeadlock() {
        SoundService sound;
        QVERIFY(sound.initialize());
        sound.setEnabled(true);
        const QVector<int> values = {26, 4, 45, 0, 48, 100, 27, 46};
        for (const int value : values) {
            sound.setVolume(value);
            QCOMPARE(sound.volume(), value);
            sound.previewFile(QStringLiteral("card_four/din.wav"));
        }
        sound.stopPreview();
    }

    void testEffectPreviewNaturalCompletionDoesNotBlockUiThread() {
        SoundService sound;
        QVERIFY(sound.initialize());
        sound.setEnabled(true);
        sound.setVolume(45);
        sound.previewFile(QStringLiteral("card_four/din.wav"));
        {
            std::lock_guard<std::mutex> lock(sound.m_mutex);
            QVERIFY(!sound.m_previewHandles.isEmpty());
        }
        QTRY_VERIFY_WITH_TIMEOUT(([&sound]() {
            std::lock_guard<std::mutex> lock(sound.m_mutex);
            return sound.m_previewHandles.isEmpty() && sound.m_waveHeaders.isEmpty();
        })(), 3000);
    }

    void testBackgroundMusicUsesControllableWaveOutPlayback() {
        SoundService sound;
        QVERIFY(sound.initialize());
        sound.setEnabled(true);
        sound.setCategoryEnabled(SoundCategory::BackgroundMusic, true);
        sound.setMusicEnabled(true);
        sound.setMusicVolume(35);

        QVERIFY(sound.playMusic(QStringLiteral("music/background.wav")));
        QVERIFY(sound.isMusicPlaying());
        QCOMPARE(sound.musicVolume(), 35);

        sound.setMusicVolume(40);
        QVERIFY(sound.isMusicPlaying());
        QCOMPARE(sound.musicVolume(), 40);

        sound.setMusicVolume(0);
        QVERIFY(sound.isMusicPlaying());
        QCOMPARE(sound.musicVolume(), 0);

        sound.setMusicVolume(100);
        QVERIFY(sound.isMusicPlaying());
        QCOMPARE(sound.musicVolume(), 100);

        sound.stopMusic();
        QVERIFY(!sound.isMusicPlaying());
    }

    void testCatalogContainsAllRuntimeVoiceFiles() {
        QCOMPARE(soundCategories().size(), static_cast<qsizetype>(SOUND_CATEGORY_COUNT));
        QSet<QString> catalogPaths;
        for (const auto& entry : soundCatalog()) {
            QVERIFY2(!entry.id.isEmpty(), qPrintable(entry.relativePath));
            QVERIFY2(!entry.displayName.isEmpty(), qPrintable(entry.relativePath));
            QVERIFY2(!entry.relativePath.isEmpty(), qPrintable(entry.id));
            catalogPaths.insert(entry.relativePath);
            const QString sourcePath = QStringLiteral(FPDZ_SOURCE_DIR "/assets/sounds/") +
                                       entry.relativePath;
            QVERIFY2(QFileInfo::exists(sourcePath), qPrintable(sourcePath));
        }

        const QStringList ranks = {
            QStringLiteral("3"), QStringLiteral("4"), QStringLiteral("5"),
            QStringLiteral("6"), QStringLiteral("7"), QStringLiteral("8"),
            QStringLiteral("9"), QStringLiteral("10"), QStringLiteral("J"),
            QStringLiteral("Q"), QStringLiteral("K"), QStringLiteral("A"),
            QStringLiteral("2"), QStringLiteral("SmallKing"), QStringLiteral("BigKing")
        };
        const QStringList patternStems = {
            QStringLiteral("three"), QStringLiteral("to"), QStringLiteral("line"),
            QStringLiteral("linkPair"), QStringLiteral("plane"), QStringLiteral("qiangbi"),
            QStringLiteral("paohong"), QStringLiteral("huojian"), QStringLiteral("daodan"),
            QStringLiteral("tianzha")
        };
        for (const QString& voice : {QStringLiteral("boy"), QStringLiteral("girl")}) {
            for (const QString& rank : ranks) {
                QVERIFY(catalogPaths.contains(
                    QStringLiteral("card_four/%1/%2.wav").arg(voice, rank)));
                QVERIFY(catalogPaths.contains(
                    QStringLiteral("card_four/%1/pair%2.wav").arg(voice, rank)));
            }
            for (const QString& stem : patternStems) {
                QVERIFY(catalogPaths.contains(
                    QStringLiteral("card_four/%1/%2.wav").arg(voice, stem)));
            }
            for (int bid = 1; bid <= 3; ++bid) {
                QVERIFY(catalogPaths.contains(
                    QStringLiteral("card_four/%1/jiao%2.wav").arg(voice).arg(bid)));
            }
            QVERIFY(catalogPaths.contains(QStringLiteral("card_four/%1/bujiao.wav").arg(voice)));
            for (int pass = 1; pass <= 4; ++pass) {
                QVERIFY(catalogPaths.contains(
                    QStringLiteral("card_four/%1/pass%2.wav").arg(voice).arg(pass)));
            }
            QVERIFY(catalogPaths.contains(QStringLiteral("card_four/%1/baojing1.wav").arg(voice)));
            QVERIFY(catalogPaths.contains(QStringLiteral("card_four/%1/baojing2.wav").arg(voice)));
        }
    }

    void testOverrideReplaceRestoreImportAndExport() {
        SoundService sound;
        QVERIFY(sound.initialize());
        SoundOverrideManager manager(sound);
        const auto* entry = findSoundCatalogEntry(QStringLiteral("card_four/din.wav"));
        QVERIFY(entry);
        const QString source = QStringLiteral(FPDZ_SOURCE_DIR "/assets/sounds/card_four/up.wav");
        QString error;
        QVERIFY2(manager.replaceSound(*entry, source, &error), qPrintable(error));
        QVERIFY(manager.hasOverride(entry->relativePath));
        QVERIFY2(manager.restoreSound(*entry, &error), qPrintable(error));
        QVERIFY(!manager.hasOverride(entry->relativePath));

        QTemporaryDir pack;
        QVERIFY(pack.isValid());
        QVERIFY(QDir().mkpath(pack.path() + QStringLiteral("/card_four")));
        QVERIFY(QFile::copy(source, pack.path() + QStringLiteral("/card_four/din.wav")));
        const auto analysis = manager.analyzePack(pack.path());
        QCOMPARE(analysis.files.size(), 1);
        QVERIFY(analysis.invalidFiles.isEmpty());
        QVERIFY2(manager.importPack(analysis, &error), qPrintable(error));
        QVERIFY(manager.hasOverride(entry->relativePath));

        QTemporaryDir currentExport;
        QVERIFY(currentExport.isValid());
        QString currentExportPath;
        QVERIFY2(manager.exportCurrentPack(currentExport.path(), &currentExportPath, &error),
                 qPrintable(error));
        QFile exportedCurrent(QDir(currentExportPath).filePath(entry->relativePath));
        QFile importedSource(source);
        QVERIFY(exportedCurrent.open(QIODevice::ReadOnly));
        QVERIFY(importedSource.open(QIODevice::ReadOnly));
        QCOMPARE(exportedCurrent.readAll(), importedSource.readAll());
        QFile currentDescription(QDir(currentExportPath).filePath(
            QString::fromUtf8(u8"音效文件对应说明.txt")));
        QVERIFY(currentDescription.open(QIODevice::ReadOnly));
        QVERIFY(currentDescription.readAll().startsWith(QByteArray::fromHex("EFBBBF")));

        QTemporaryDir exported;
        QVERIFY(exported.isValid());
        QString exportedPath;
        QVERIFY2(manager.exportTemplate(exported.path(), &exportedPath, &error), qPrintable(error));
        QFile description(exportedPath + QString::fromUtf8(u8"/音效文件对应说明.txt"));
        QVERIFY(description.open(QIODevice::ReadOnly));
        const QByteArray bytes = description.readAll();
        QVERIFY(bytes.startsWith(QByteArray::fromHex("EFBBBF")));
        QVERIFY2(manager.restoreAll(&error) >= 1, qPrintable(error));
    }

    void testDialogControlsAndActiveGameRestrictions() {
        AppSettings settings;
        SoundService sound;
        QVERIFY(sound.initialize());
        SoundManagerDialog dialog(settings, sound, true);
        auto* categoryCombo = dialog.findChild<QComboBox*>(QStringLiteral("soundCategoryComboBox"));
        auto* entryCombo = dialog.findChild<QComboBox*>(QStringLiteral("soundEntryComboBox"));
        auto* categoryEnabled = dialog.findChild<QCheckBox*>(
            QStringLiteral("soundCategoryEnabledCheckBox"));
        auto* confirm = dialog.findChild<QPushButton*>(
            QStringLiteral("confirmSoundReplacementButton"));
        auto* restoreCurrent = dialog.findChild<QPushButton*>(
            QStringLiteral("restoreCurrentSoundButton"));
        auto* exportCurrent = dialog.findChild<QPushButton*>(
            QStringLiteral("exportCurrentSoundPackButton"));
        QVERIFY(categoryCombo);
        QVERIFY(entryCombo);
        QVERIFY(categoryEnabled);
        QVERIFY(confirm);
        QVERIFY(restoreCurrent);
        QVERIFY(exportCurrent);
        QCOMPARE(categoryCombo->count(), static_cast<int>(SOUND_CATEGORY_COUNT));
        QVERIFY(entryCombo->count() >= 2);
        QVERIFY(!confirm->isEnabled());
        QVERIFY(exportCurrent->isEnabled());

        SoundOverrideManager manager(sound);
        const auto* entry = findSoundCatalogEntry(QStringLiteral("card_four/din.wav"));
        QVERIFY(entry);
        const QString source = QStringLiteral(FPDZ_SOURCE_DIR "/assets/sounds/card_four/up.wav");
        QString error;
        QVERIFY2(manager.replaceSound(*entry, source, &error), qPrintable(error));
        const int yourTurnIndex = categoryCombo->findData(static_cast<int>(SoundCategory::YourTurn));
        QVERIFY(yourTurnIndex >= 0);
        categoryCombo->setCurrentIndex(yourTurnIndex);
        QVERIFY(restoreCurrent->isEnabled());

        bool sawConfirmation = false;
        bool defaultedToCancel = false;
        QTimer::singleShot(0, this, [&]() {
            auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            sawConfirmation = message != nullptr;
            if (message) {
                defaultedToCancel = message->defaultButton() == message->button(QMessageBox::No);
                message->done(QMessageBox::No);
            }
        });
        restoreCurrent->click();
        QVERIFY(sawConfirmation);
        QVERIFY(defaultedToCancel);
        QVERIFY(manager.hasOverride(entry->relativePath));
        QVERIFY2(manager.restoreAll(&error) >= 1, qPrintable(error));

        SoundManagerDialog activeDialog(settings, sound, false);
        auto* activeConfirm = activeDialog.findChild<QPushButton*>(
            QStringLiteral("confirmSoundReplacementButton"));
        auto* activeExportCurrent = activeDialog.findChild<QPushButton*>(
            QStringLiteral("exportCurrentSoundPackButton"));
        QVERIFY(activeConfirm);
        QVERIFY(activeExportCurrent);
        QVERIFY(!activeConfirm->isEnabled());
        QVERIFY(!activeExportCurrent->isEnabled());
    }
};

QTEST_MAIN(TestSoundManagement)
#include "test_sound_management.moc"
