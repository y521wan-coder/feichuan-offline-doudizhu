#include "sound_service.h"
#include "../persistence/data_paths.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QResource>
#include <QDebug>
#include <QStringList>
#include <QtEndian>
#include <algorithm>

#pragma comment(lib, "winmm.lib")

namespace fpdz {

namespace {

constexpr qint64 kMaximumCustomWaveBytes = 100LL * 1024LL * 1024LL;

bool parseWaveFile(const QString& filePath, WaveBuffer* output, QString* error) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QString::fromUtf8(u8"无法读取 WAV 文件");
        return false;
    }
    if (file.size() <= 0 || file.size() > kMaximumCustomWaveBytes) {
        if (error) *error = QString::fromUtf8(u8"WAV 文件必须大于0字节且不超过100 MB");
        return false;
    }
    const QByteArray raw = file.readAll();
    if (raw.size() < 44) {
        if (error) *error = QString::fromUtf8(u8"WAV 文件过小或内容不完整");
        return false;
    }

    const char* data = raw.constData();
    if (memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0) {
        if (error) *error = QString::fromUtf8(u8"文件不是有效的 RIFF/WAVE 音频");
        return false;
    }

    WAVEFORMATEX format{};
    bool foundFormat = false;
    bool foundData = false;
    int dataOffset = 0;
    int dataSize = 0;
    int position = 12;
    while (position <= raw.size() - 8) {
        const quint32 chunkSize = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar*>(data + position + 4));
        const qint64 chunkEnd = static_cast<qint64>(position) + 8 + chunkSize;
        if (chunkEnd > raw.size()) {
            if (error) *error = QString::fromUtf8(u8"WAV 数据块长度无效");
            return false;
        }
        if (memcmp(data + position, "fmt ", 4) == 0 && chunkSize >= 16) {
            const uchar* value = reinterpret_cast<const uchar*>(data + position + 8);
            format.wFormatTag = qFromLittleEndian<quint16>(value);
            format.nChannels = qFromLittleEndian<quint16>(value + 2);
            format.nSamplesPerSec = qFromLittleEndian<quint32>(value + 4);
            format.nAvgBytesPerSec = qFromLittleEndian<quint32>(value + 8);
            format.nBlockAlign = qFromLittleEndian<quint16>(value + 12);
            format.wBitsPerSample = qFromLittleEndian<quint16>(value + 14);
            format.cbSize = 0;
            foundFormat = true;
        } else if (memcmp(data + position, "data", 4) == 0) {
            dataOffset = position + 8;
            dataSize = static_cast<int>(chunkSize);
            foundData = true;
        }
        position = static_cast<int>(chunkEnd + (chunkSize % 2));
    }

    if (!foundFormat || !foundData || dataSize <= 0) {
        if (error) *error = QString::fromUtf8(u8"WAV 缺少有效的 fmt 或 data 数据块");
        return false;
    }
    if (format.wFormatTag != WAVE_FORMAT_PCM || format.nAvgBytesPerSec == 0) {
        if (error) *error = QString::fromUtf8(u8"当前仅支持未压缩 PCM WAV 文件");
        return false;
    }

    if (output) {
        output->format = format;
        output->data.resize(dataSize);
        memcpy(output->data.data(), data + dataOffset, dataSize);
    }
    return true;
}

} // namespace

SoundService::SoundService(QObject* parent) : QObject(parent) {
    m_sequenceTimer.setSingleShot(true);
    connect(&m_sequenceTimer, &QTimer::timeout,
            this, &SoundService::playNextQueuedFile);
}

SoundService::~SoundService() {
    shutdown();
}

bool SoundService::initialize() {
    if (m_initialized) return true;
    m_initialized = true;
    return true;
}

void SoundService::shutdown() {
    m_enabled = false;
    stopAll();
    m_resolvedPaths.clear();
    m_sounds.clear();
    m_initialized = false;
}

QString SoundService::resolveSoundPath(const QString& relativePath) const {
    const QString normalized = QDir::fromNativeSeparators(relativePath);
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        DataPaths::customSoundsDir() + "/" + normalized,
        appDir + "/resources/sounds/" + normalized,
        QDir::currentPath() + "/resources/sounds/" + normalized,
        QDir::currentPath() + "/" + normalized,
        ":/sounds/" + normalized
    };

    for (const QString& candidate : candidates) {
        if (candidate.startsWith(":/")) {
            if (QFile::exists(candidate)) return candidate;
        } else if (QFileInfo::exists(candidate)) {
            if (candidate.startsWith(DataPaths::customSoundsDir() + QStringLiteral("/"))) {
                QString validationError;
                if (!parseWaveFile(candidate, nullptr, &validationError)) {
                    qWarning() << "SoundService: invalid custom sound, using default"
                               << candidate << validationError;
                    continue;
                }
            }
            return candidate;
        }
    }
    return {};
}

bool SoundService::loadSound(const QString& key, const QString& filePath) {
    WaveBuffer buffer;
    QString error;
    if (!parseWaveFile(filePath, &buffer, &error)) {
        qWarning() << "SoundService:" << error << filePath;
        return false;
    }
    m_sounds[key] = std::move(buffer);
    return true;
}

void SoundService::play(SoundId id) {
    playFile(soundFileName(id), soundCategory(id));
}

void SoundService::playFile(const QString& relativePath, SoundCategory category) {
    playFiles({SoundRequest{relativePath, category}});
}

int SoundService::playFiles(const QVector<SoundRequest>& requests) {
    QStringList files;
    for (const auto& request : requests) {
        if (isCategoryEnabled(request.category)) files.push_back(request.relativePath);
    }
    return queueFiles(files);
}

void SoundService::previewFile(const QString& relativePath) {
    if (!m_enabled || !m_initialized || relativePath.isEmpty()) return;
    stopPreview();

    const QString key = QDir::fromNativeSeparators(relativePath);
    QString resolvedPath;
    if (m_resolvedPaths.contains(key)) {
        resolvedPath = m_resolvedPaths.value(key);
    } else {
        resolvedPath = resolveSoundPath(key);
        m_resolvedPaths.insert(key, resolvedPath);
    }
    if (resolvedPath.isEmpty() || resolvedPath.startsWith(":/")) return;
    if (!m_sounds.contains(key) && !loadSound(key, resolvedPath)) return;
    playWaveBuffer(m_sounds.value(key), true);
}

int SoundService::queueFiles(const QStringList& relativePaths) {
    if (!m_enabled || !m_initialized) return 0;
    m_sequenceTimer.stop();
    m_queuedFiles.clear();
    stopEffectPlayback();

    int totalDuration = 0;
    for (const auto& relativePath : relativePaths) {
        if (relativePath.isEmpty()) continue;
        const int duration = soundDurationMilliseconds(relativePath);
        if (duration <= 0) continue;
        m_queuedFiles.push_back(relativePath);
        totalDuration += duration + 40;
    }
    playNextQueuedFile();
    return totalDuration;
}

int SoundService::soundDurationMilliseconds(const QString& relativePath) {
    const QString key = QDir::fromNativeSeparators(relativePath);
    QString resolvedPath;
    if (m_resolvedPaths.contains(key)) {
        resolvedPath = m_resolvedPaths.value(key);
    } else {
        resolvedPath = resolveSoundPath(key);
        m_resolvedPaths.insert(key, resolvedPath);
    }
    if (resolvedPath.isEmpty() || resolvedPath.startsWith(":/")) {
        qWarning() << "SoundService: cannot resolve" << key;
        return 0;
    }
    if (!m_sounds.contains(key) && !loadSound(key, resolvedPath)) return 0;
    const auto& sound = m_sounds[key];
    if (sound.format.nAvgBytesPerSec == 0) return 0;
    return std::max(1, static_cast<int>(
        static_cast<qint64>(sound.data.size()) * 1000 / sound.format.nAvgBytesPerSec));
}

void SoundService::playNextQueuedFile() {
    if (!m_enabled || !m_initialized || m_queuedFiles.isEmpty()) return;
    const QString relativePath = m_queuedFiles.takeFirst();
    const int duration = soundDurationMilliseconds(relativePath);
    if (duration <= 0) {
        m_sequenceTimer.start(0);
        return;
    }
    playFileImmediate(relativePath);
    if (!m_queuedFiles.isEmpty()) m_sequenceTimer.start(duration + 40);
}

void SoundService::playFileImmediate(const QString& relativePath) {
    if (!m_enabled || !m_initialized || relativePath.isEmpty()) return;

    const QString key = QDir::fromNativeSeparators(relativePath);
    QString resolvedPath;
    if (m_resolvedPaths.contains(key)) {
        resolvedPath = m_resolvedPaths.value(key);
    } else {
        resolvedPath = resolveSoundPath(key);
        m_resolvedPaths.insert(key, resolvedPath);
    }
    if (resolvedPath.isEmpty() || resolvedPath.startsWith(":/")) {
        qWarning() << "SoundService: cannot resolve" << key;
        return;
    }

    if (!m_sounds.contains(key) && !loadSound(key, resolvedPath)) return;
    playWaveBuffer(m_sounds.value(key));
}

void SoundService::stopWaveHandles(QVector<HWAVEOUT>& trackedHandles) {
    QVector<std::pair<HWAVEOUT, WAVEHDR*>> handles;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto hwo : trackedHandles) {
            handles.push_back({hwo, m_waveHeaders.value(hwo, nullptr)});
            m_waveHeaders.remove(hwo);
        }
        trackedHandles.clear();
    }

    for (const auto& [hwo, hdr] : handles) {
        waveOutReset(hwo);
        if (hdr && (hdr->dwFlags & WHDR_PREPARED) != 0) {
            waveOutUnprepareHeader(hwo, hdr, sizeof(WAVEHDR));
        }
        waveOutClose(hwo);
        if (hdr) {
            auto* data = reinterpret_cast<QVector<BYTE>*>(hdr->dwUser);
            delete data;
            delete hdr;
        }
    }
}

void SoundService::finishWaveHandle(HWAVEOUT hwo) {
    WAVEHDR* hdr = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        hdr = m_waveHeaders.value(hwo, nullptr);
    }
    if (!hdr) return;
    if ((hdr->dwFlags & WHDR_DONE) == 0) {
        QTimer::singleShot(20, this, [this, hwo]() {
            finishWaveHandle(hwo);
        });
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_waveHeaders.value(hwo, nullptr) != hdr) return;
        m_openHandles.removeAll(hwo);
        m_previewHandles.removeAll(hwo);
        m_waveHeaders.remove(hwo);
    }
    if ((hdr->dwFlags & WHDR_PREPARED) != 0) {
        waveOutUnprepareHeader(hwo, hdr, sizeof(WAVEHDR));
    }
    waveOutClose(hwo);
    auto* data = reinterpret_cast<QVector<BYTE>*>(hdr->dwUser);
    delete data;
    delete hdr;
}

void SoundService::stopEffectPlayback() {
    stopWaveHandles(m_openHandles);
}

void SoundService::stopPreview() {
    stopWaveHandles(m_previewHandles);
}

void SoundService::stopEffects() {
    m_sequenceTimer.stop();
    m_queuedFiles.clear();
    PlaySoundW(nullptr, nullptr, 0);
    stopEffectPlayback();
    stopPreview();
}

void SoundService::stopAll() {
    stopEffects();
    stopMusic();
}

void SoundService::setEnabled(bool enabled) {
    m_enabled = enabled;
    if (!enabled) stopEffects();
}

void SoundService::setVolume(int volumePercent) {
    m_volume = qBound(0, volumePercent, 100);
}

void SoundService::setCategoryEnabled(SoundCategory category, bool enabled) {
    if (category == SoundCategory::Count) return;
    m_categorySettings[soundCategoryIndex(category)] = enabled;
    if (category == SoundCategory::BackgroundMusic && !enabled) stopMusic();
}

bool SoundService::isCategoryEnabled(SoundCategory category) const {
    if (category == SoundCategory::Count) return false;
    return m_categorySettings[soundCategoryIndex(category)];
}

void SoundService::setCategorySettings(const SoundCategorySettings& settings) {
    m_categorySettings = settings;
    if (!isCategoryEnabled(SoundCategory::BackgroundMusic)) stopMusic();
}

void SoundService::setMusicEnabled(bool enabled) {
    m_musicEnabled = enabled;
    if (!enabled) stopMusic();
}

void SoundService::setMusicVolume(int volumePercent) {
    const int clamped = qBound(0, volumePercent, 100);
    if (m_musicVolume == clamped) return;
    m_musicVolume = clamped;
    if (m_musicHandle) {
        const QString current = m_currentMusic;
        const bool loop = m_musicLooping;
        stopMusic();
        startMusic(current, loop);
    }
}

bool SoundService::playMusic(const QString& relativePath) {
    if (!m_musicEnabled || !m_initialized || relativePath.isEmpty() ||
        !isCategoryEnabled(SoundCategory::BackgroundMusic)) return false;
    const QString key = QDir::fromNativeSeparators(relativePath);
    if (m_musicHandle && m_musicLooping && m_currentMusic == key) return true;
    return startMusic(key, true);
}

bool SoundService::previewMusic(const QString& relativePath) {
    if (!m_enabled || !m_initialized || relativePath.isEmpty()) return false;
    return startMusic(QDir::fromNativeSeparators(relativePath), false);
}

void SoundService::stopMusic() {
    if (m_musicHandle) {
        waveOutReset(m_musicHandle);
        if ((m_musicHeader.dwFlags & WHDR_PREPARED) != 0) {
            waveOutUnprepareHeader(m_musicHandle, &m_musicHeader, sizeof(WAVEHDR));
        }
        waveOutClose(m_musicHandle);
    }
    m_musicHandle = nullptr;
    m_musicHeader = {};
    m_musicData.clear();
    m_musicLooping = false;
    m_currentMusic.clear();
}

bool SoundService::startMusic(const QString& relativePath, bool loop) {
    const QString key = QDir::fromNativeSeparators(relativePath);
    const QString resolvedPath = resolveSoundPath(key);
    if (resolvedPath.isEmpty() || resolvedPath.startsWith(":/")) return false;

    WaveBuffer buffer;
    QString error;
    if (!parseWaveFile(resolvedPath, &buffer, &error)) {
        qWarning() << "SoundService: cannot load background music" << resolvedPath << error;
        return false;
    }
    applyMusicVolume(buffer.data, buffer.format);
    stopMusic();
    MMRESULT result = waveOutOpen(&m_musicHandle, WAVE_MAPPER, &buffer.format,
                                  0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR) {
        qWarning() << "SoundService: cannot open background music device" << result;
        stopMusic();
        return false;
    }
    m_musicData = std::move(buffer.data);
    m_musicHeader = {};
    m_musicHeader.lpData = reinterpret_cast<LPSTR>(m_musicData.data());
    m_musicHeader.dwBufferLength = static_cast<DWORD>(m_musicData.size());
    if (loop) {
        m_musicHeader.dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP;
        m_musicHeader.dwLoops = 0xFFFFFFFF;
    }

    result = waveOutPrepareHeader(m_musicHandle, &m_musicHeader, sizeof(WAVEHDR));
    if (result != MMSYSERR_NOERROR) {
        qWarning() << "SoundService: cannot prepare background music" << result;
        stopMusic();
        return false;
    }
    result = waveOutWrite(m_musicHandle, &m_musicHeader, sizeof(WAVEHDR));
    if (result != MMSYSERR_NOERROR) {
        qWarning() << "SoundService: cannot start background music" << result;
        stopMusic();
        return false;
    }

    m_currentMusic = key;
    m_musicLooping = loop;
    return true;
}

void SoundService::invalidateSound(const QString& relativePath) {
    const QString key = QDir::fromNativeSeparators(relativePath);
    m_resolvedPaths.remove(key);
    m_sounds.remove(key);
}

void SoundService::invalidateAllSounds() {
    m_resolvedPaths.clear();
    m_sounds.clear();
}

QString SoundService::resolvedSoundPath(const QString& relativePath) const {
    return resolveSoundPath(relativePath);
}

bool SoundService::validateWaveFile(const QString& filePath, QString* error) {
    return parseWaveFile(filePath, nullptr, error);
}

QString SoundService::soundFileName(SoundId id) {
    switch (id) {
    case SoundId::CardPlay:     return "card_four/give.wav";
    case SoundId::CardPass:     return "card_four/pass.wav";
    case SoundId::Bomb:         return "card_four/qiangbi.wav";
    case SoundId::Rocket:       return "card_four/huojian.wav";
    case SoundId::TurnChange:   return "card_four/position.wav";
    case SoundId::YourTurn:     return "card_four/din.wav";
    case SoundId::Win:          return "card_four/win.wav";
    case SoundId::Lose:         return "card_four/fail.wav";
    case SoundId::CardSelect:   return "card_four/up.wav";
    case SoundId::CardDeselect: return "card_four/move.wav";
    case SoundId::DealCards:    return "card_four/start.wav";
    case SoundId::Bid:          return "card_four/robLandlord.wav";
    case SoundId::Invalid:      return "card_four/GiveError.wav";
    case SoundId::LowCards:     return "card_four/baojing.wav";
    case SoundId::Multiplier:   return "card_four/qiangbi.wav";
    case SoundId::Landlord:     return "card_four/robLandlord.wav";
    case SoundId::ButtonClick:  return "card_four/din.wav";
    case SoundId::GameStart:    return "card_four/BeginGame.wav";
    }
    return {};
}

SoundCategory SoundService::soundCategory(SoundId id) {
    switch (id) {
    case SoundId::CardPlay:
    case SoundId::Bomb:
    case SoundId::Rocket:
        return SoundCategory::CardPattern;
    case SoundId::CardPass:
        return SoundCategory::Pass;
    case SoundId::YourTurn:
        return SoundCategory::YourTurn;
    case SoundId::Win:
    case SoundId::Lose:
        return SoundCategory::GameResult;
    case SoundId::CardSelect:
    case SoundId::CardDeselect:
        return SoundCategory::CardSelection;
    case SoundId::Bid:
    case SoundId::Landlord:
        return SoundCategory::BiddingLandlord;
    case SoundId::Invalid:
        return SoundCategory::InvalidAction;
    case SoundId::LowCards:
        return SoundCategory::LowCards;
    case SoundId::Multiplier:
        return SoundCategory::Multiplier;
    case SoundId::TurnChange:
    case SoundId::DealCards:
    case SoundId::ButtonClick:
    case SoundId::GameStart:
        return SoundCategory::StartupDeal;
    }
    return SoundCategory::StartupDeal;
}

void SoundService::applyVolume(QVector<BYTE>& pcmData, const WAVEFORMATEX& fmt) {
    if (m_volume >= 100) return;
    float gain = static_cast<float>(m_volume) / 100.0f;

    if (fmt.wBitsPerSample == 16) {
        auto* samples = reinterpret_cast<int16_t*>(pcmData.data());
        int count = pcmData.size() / 2;
        for (int i = 0; i < count; ++i) {
            samples[i] = static_cast<int16_t>(samples[i] * gain);
        }
    } else if (fmt.wBitsPerSample == 8) {
        for (int i = 0; i < pcmData.size(); ++i) {
            int val = static_cast<int>(pcmData[i]) - 128;
            pcmData[i] = static_cast<BYTE>(static_cast<int>(val * gain) + 128);
        }
    }
}

void SoundService::applyMusicVolume(QVector<BYTE>& pcmData, const WAVEFORMATEX& fmt) {
    if (m_musicVolume >= 100) return;
    const float gain = static_cast<float>(m_musicVolume) / 100.0f;

    if (fmt.wBitsPerSample == 16) {
        auto* samples = reinterpret_cast<int16_t*>(pcmData.data());
        const int count = pcmData.size() / 2;
        for (int i = 0; i < count; ++i) {
            samples[i] = static_cast<int16_t>(samples[i] * gain);
        }
    } else if (fmt.wBitsPerSample == 8) {
        for (int i = 0; i < pcmData.size(); ++i) {
            const int value = static_cast<int>(pcmData[i]) - 128;
            pcmData[i] = static_cast<BYTE>(static_cast<int>(value * gain) + 128);
        }
    }
}

bool SoundService::playWaveBuffer(const WaveBuffer& originalBuf, bool preview) {
    WaveBuffer buf;
    buf.format = originalBuf.format;
    buf.data = originalBuf.data;
    applyVolume(buf.data, buf.format);

    HWAVEOUT hwo = nullptr;
    MMRESULT result = waveOutOpen(&hwo, WAVE_MAPPER, &buf.format,
                                  0, 0, CALLBACK_NULL);

    if (result != MMSYSERR_NOERROR) {
        return false;
    }

    auto* hdr = new WAVEHDR();
    memset(hdr, 0, sizeof(WAVEHDR));
    auto* persistentData = new QVector<BYTE>(buf.data);
    hdr->lpData = reinterpret_cast<LPSTR>(persistentData->data());
    hdr->dwBufferLength = static_cast<DWORD>(persistentData->size());
    hdr->dwUser = reinterpret_cast<DWORD_PTR>(persistentData);

    result = waveOutPrepareHeader(hwo, hdr, sizeof(WAVEHDR));
    if (result != MMSYSERR_NOERROR) {
        waveOutClose(hwo);
        delete persistentData;
        delete hdr;
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_waveHeaders.insert(hwo, hdr);
        if (preview) {
            m_previewHandles.append(hwo);
        } else {
            m_openHandles.append(hwo);
        }
    }

    result = waveOutWrite(hwo, hdr, sizeof(WAVEHDR));
    if (result != MMSYSERR_NOERROR) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_openHandles.removeAll(hwo);
            m_previewHandles.removeAll(hwo);
            m_waveHeaders.remove(hwo);
        }
        waveOutUnprepareHeader(hwo, hdr, sizeof(WAVEHDR));
        waveOutClose(hwo);
        delete persistentData;
        delete hdr;
        return false;
    }

    const int durationMilliseconds = std::max(1, static_cast<int>(
        static_cast<qint64>(buf.data.size()) * 1000 / buf.format.nAvgBytesPerSec));
    QTimer::singleShot(durationMilliseconds + 20, this, [this, hwo]() {
        finishWaveHandle(hwo);
    });

    return true;
}

} // namespace fpdz
