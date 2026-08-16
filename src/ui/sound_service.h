#pragma once

#include "../core/audio/sound_category.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QTimer>
#include <QVector>
#include <memory>
#include <mutex>
#include <windows.h>
#include <mmsystem.h>

class TestSoundManagement;

namespace fpdz {

enum class SoundId {
    CardPlay,
    CardPass,
    Bomb,
    Rocket,
    TurnChange,
    YourTurn,
    Win,
    Lose,
    CardSelect,
    CardDeselect,
    DealCards,
    Bid,
    Invalid,
    LowCards,
    Multiplier,
    Landlord,
    ButtonClick,
    GameStart
};

struct WaveBuffer {
    QVector<BYTE> data;
    WAVEFORMATEX format;
};

struct SoundRequest {
    QString relativePath;
    SoundCategory category = SoundCategory::StartupDeal;
};

class SoundService : public QObject {
    Q_OBJECT
public:
    explicit SoundService(QObject* parent = nullptr);
    ~SoundService() override;

    bool initialize();
    void shutdown();

    void play(SoundId id);
    void playFile(const QString& relativePath, SoundCategory category);
    int playFiles(const QVector<SoundRequest>& requests);
    void previewFile(const QString& relativePath);
    void stopPreview();
    void stopEffects();
    void stopAll();

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    void setVolume(int volumePercent);
    int volume() const { return m_volume; }

    void setCategoryEnabled(SoundCategory category, bool enabled);
    bool isCategoryEnabled(SoundCategory category) const;
    void setCategorySettings(const SoundCategorySettings& settings);
    SoundCategorySettings categorySettings() const { return m_categorySettings; }

    void setMusicEnabled(bool enabled);
    bool isMusicEnabled() const { return m_musicEnabled; }
    void setMusicVolume(int volumePercent);
    int musicVolume() const { return m_musicVolume; }
    bool playMusic(const QString& relativePath);
    bool previewMusic(const QString& relativePath);
    void stopMusic();
    bool isMusicPlaying() const { return m_musicHandle != nullptr; }

    void invalidateSound(const QString& relativePath);
    void invalidateAllSounds();
    QString resolvedSoundPath(const QString& relativePath) const;

    static QString soundFileName(SoundId id);
    static SoundCategory soundCategory(SoundId id);
    static bool validateWaveFile(const QString& filePath, QString* error = nullptr);

private:
    friend class ::TestSoundManagement;

    QString resolveSoundPath(const QString& relativePath) const;
    bool loadSound(const QString& key, const QString& filePath);
    int queueFiles(const QStringList& relativePaths);
    int soundDurationMilliseconds(const QString& relativePath);
    void playNextQueuedFile();
    void playFileImmediate(const QString& relativePath);
    void applyVolume(QVector<BYTE>& pcmData, const WAVEFORMATEX& fmt);
    void applyMusicVolume(QVector<BYTE>& pcmData, const WAVEFORMATEX& fmt);
    bool playWaveBuffer(const WaveBuffer& buf, bool preview = false);
    bool startMusic(const QString& relativePath, bool loop);
    void stopEffectPlayback();
    void stopWaveHandles(QVector<HWAVEOUT>& handles);
    void finishWaveHandle(HWAVEOUT hwo);

    bool m_enabled = true;
    int m_volume = 80;
    SoundCategorySettings m_categorySettings = [] {
        SoundCategorySettings values{};
        values.fill(true);
        return values;
    }();
    bool m_musicEnabled = false;
    int m_musicVolume = 35;
    QString m_currentMusic;
    HWAVEOUT m_musicHandle = nullptr;
    WAVEHDR m_musicHeader{};
    QVector<BYTE> m_musicData;
    bool m_musicLooping = false;
    bool m_initialized = false;
    QMap<QString, QString> m_resolvedPaths;
    QMap<QString, WaveBuffer> m_sounds;
    QStringList m_queuedFiles;
    QTimer m_sequenceTimer;
    QVector<HWAVEOUT> m_openHandles;
    QVector<HWAVEOUT> m_previewHandles;
    QMap<HWAVEOUT, WAVEHDR*> m_waveHeaders;
    std::mutex m_mutex;
};

} // namespace fpdz
