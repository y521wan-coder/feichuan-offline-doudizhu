#include "online_storage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include <windows.h>
#include <dpapi.h>

namespace fpdz {

namespace {
QString sessionPath() { return OnlineStorage::dataDir() + QStringLiteral("/session.dpapi"); }
}

QString OnlineStorage::dataDir() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString path = base + QStringLiteral("/online");
    QDir().mkpath(path);
    return path;
}

QString OnlineStorage::readSession() {
    QFile file(sessionPath());
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 8192) return {};
    QByteArray cipher = file.readAll();
    DATA_BLOB input{DWORD(cipher.size()), reinterpret_cast<BYTE*>(cipher.data())};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) return {};
    QByteArray plain(reinterpret_cast<const char*>(output.pbData), qsizetype(output.cbData));
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    const QString token = QString::fromUtf8(plain);
    plain.fill('\0');
    return token;
}

bool OnlineStorage::writeSession(const QString& token) {
    QByteArray plain = token.toUtf8();
    DATA_BLOB input{DWORD(plain.size()), reinterpret_cast<BYTE*>(plain.data())};
    DATA_BLOB output{};
    const bool encrypted = CryptProtectData(&input, L"Feichuan online session",
                                           nullptr, nullptr, nullptr,
                                           CRYPTPROTECT_UI_FORBIDDEN, &output);
    plain.fill('\0');
    if (!encrypted) return false;
    QSaveFile file(sessionPath());
    bool written = file.open(QIODevice::WriteOnly) &&
                   file.write(reinterpret_cast<const char*>(output.pbData), output.cbData)
                       == qint64(output.cbData) && file.commit();
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return written;
}

void OnlineStorage::clearSession() { QFile::remove(sessionPath()); }

QString OnlineStorage::emojiSoundPath(int id) {
    if (id < 1 || id > 8) return {};
    const QString path = dataDir() + QStringLiteral("/emoji/%1.wav").arg(id);
    return QFileInfo::exists(path) ? path : QString();
}

bool OnlineStorage::importEmojiSound(int id, const QString& source, QString* error) {
    if (id < 1 || id > 8) {
        if (error) *error = QStringLiteral("表情编号无效");
        return false;
    }
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly) || input.size() < 44 || input.size() > 20 * 1024 * 1024) {
        if (error) *error = QStringLiteral("请选择不超过 20 MB 的 WAV 文件");
        return false;
    }
    const QByteArray header = input.peek(12);
    if (header.size() != 12 || header.left(4) != "RIFF" || header.mid(8, 4) != "WAVE") {
        if (error) *error = QStringLiteral("文件不是 WAV 音频");
        return false;
    }
    const QString dir = dataDir() + QStringLiteral("/emoji");
    if (!QDir().mkpath(dir)) return false;
    QSaveFile output(dir + QStringLiteral("/%1.wav").arg(id));
    if (!output.open(QIODevice::WriteOnly)) return false;
    while (!input.atEnd()) {
        const QByteArray block = input.read(64 * 1024);
        if (block.isEmpty() || output.write(block) != block.size()) return false;
    }
    return output.commit();
}

bool OnlineStorage::removeEmojiSound(int id) {
    if (id < 1 || id > 8) return false;
    return QFile::remove(dataDir() + QStringLiteral("/emoji/%1.wav").arg(id));
}

} // namespace fpdz
