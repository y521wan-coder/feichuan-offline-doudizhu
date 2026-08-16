#include "sound_override_manager.h"

#include "sound_service.h"
#include "../persistence/data_paths.h"

#include <QDir>
#include <QDirIterator>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>

namespace fpdz {

SoundOverrideManager::SoundOverrideManager(SoundService& soundService)
    : m_soundService(soundService) {}

QString SoundOverrideManager::overridePath(const QString& relativePath) const {
    return QDir::cleanPath(DataPaths::customSoundsDir() + QStringLiteral("/") +
                           QDir::fromNativeSeparators(relativePath));
}

bool SoundOverrideManager::hasOverride(const QString& relativePath) const {
    const QFileInfo file(overridePath(relativePath));
    return file.exists() && file.isFile() && file.size() > 0;
}

bool SoundOverrideManager::copyValidatedWave(const QString& sourcePath,
                                             const QString& relativePath,
                                             QString* error) {
    QString validationError;
    if (!SoundService::validateWaveFile(sourcePath, &validationError)) {
        if (error) *error = validationError;
        return false;
    }

    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        if (error) *error = QString::fromUtf8(u8"无法读取所选 WAV 文件");
        return false;
    }
    const QByteArray data = source.readAll();
    const QString targetPath = overridePath(relativePath);
    if (!QDir().mkpath(QFileInfo(targetPath).absolutePath())) {
        if (error) *error = QString::fromUtf8(u8"无法创建自定义音效目录");
        return false;
    }
    QSaveFile target(targetPath);
    if (!target.open(QIODevice::WriteOnly) || target.write(data) != data.size() ||
        !target.commit()) {
        if (error) *error = QString::fromUtf8(u8"无法保存自定义音效文件");
        return false;
    }
    m_soundService.invalidateSound(relativePath);
    if (!hasOverride(relativePath) ||
        QDir::cleanPath(m_soundService.resolvedSoundPath(relativePath)) !=
            QDir::cleanPath(targetPath)) {
        if (error) *error = QString::fromUtf8(u8"自定义音效已写入，但运行时未能切换到新文件");
        qWarning() << "SoundOverrideManager: override verification failed" << relativePath;
        return false;
    }
    qInfo() << "SoundOverrideManager: replaced" << relativePath;
    return true;
}

bool SoundOverrideManager::replaceSound(const SoundCatalogEntry& entry,
                                        const QString& sourcePath, QString* error) {
    return copyValidatedWave(sourcePath, entry.relativePath, error);
}

bool SoundOverrideManager::restoreSound(const SoundCatalogEntry& entry, QString* error) {
    const QString path = overridePath(entry.relativePath);
    if (QFileInfo::exists(path) && !QFile::remove(path)) {
        if (error) *error = QString::fromUtf8(u8"无法删除自定义音效文件");
        return false;
    }
    m_soundService.invalidateSound(entry.relativePath);
    qInfo() << "SoundOverrideManager: restored" << entry.relativePath;
    return true;
}

int SoundOverrideManager::restoreCategory(SoundCategory category, QString* error) {
    int restored = 0;
    QSet<QString> processed;
    for (const auto& entry : soundCatalog()) {
        if (entry.category != category || processed.contains(entry.relativePath)) continue;
        processed.insert(entry.relativePath);
        if (hasOverride(entry.relativePath)) {
            if (!restoreSound(entry, error)) return -1;
            ++restored;
        }
    }
    return restored;
}

int SoundOverrideManager::restoreAll(QString* error) {
    int restored = 0;
    QSet<QString> processed;
    for (const auto& entry : soundCatalog()) {
        if (processed.contains(entry.relativePath)) continue;
        processed.insert(entry.relativePath);
        if (hasOverride(entry.relativePath)) {
            if (!restoreSound(entry, error)) return -1;
            ++restored;
        }
    }
    return restored;
}

SoundPackAnalysis SoundOverrideManager::analyzePack(const QString& folderPath) const {
    SoundPackAnalysis analysis;
    const QDir root(folderPath);
    QSet<QString> knownPaths;
    for (const auto& entry : soundCatalog()) {
        const QString normalized = QDir::fromNativeSeparators(entry.relativePath).toLower();
        knownPaths.insert(normalized);
        const QString sourcePath = root.filePath(entry.relativePath);
        if (!QFileInfo::exists(sourcePath)) continue;
        QString validationError;
        if (SoundService::validateWaveFile(sourcePath, &validationError)) {
            analysis.files.push_back({entry, sourcePath});
        } else {
            analysis.invalidFiles.push_back(entry.relativePath + QStringLiteral(": ") +
                                            validationError);
        }
    }

    QDirIterator iterator(folderPath, {QStringLiteral("*.wav")}, QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString absolutePath = iterator.next();
        const QString relativePath = root.relativeFilePath(absolutePath);
        if (!knownPaths.contains(QDir::fromNativeSeparators(relativePath).toLower())) {
            analysis.unknownFiles.push_back(QDir::fromNativeSeparators(relativePath));
        }
    }
    return analysis;
}

bool SoundOverrideManager::importPack(const SoundPackAnalysis& analysis, QString* error) {
    if (!analysis.invalidFiles.isEmpty()) {
        if (error) *error = QString::fromUtf8(u8"音效包中包含无效 WAV，未导入任何文件");
        return false;
    }
    if (analysis.files.isEmpty()) {
        if (error) *error = QString::fromUtf8(u8"所选文件夹中没有可识别的音效文件");
        return false;
    }
    for (const auto& file : analysis.files) {
        if (!copyValidatedWave(file.sourcePath, file.entry.relativePath, error)) return false;
    }
    qInfo() << "SoundOverrideManager: imported pack files" << analysis.files.size();
    return true;
}

bool SoundOverrideManager::exportTemplate(const QString& parentFolder,
                                          QString* exportedPath, QString* error) const {
    const QString templatePath = QDir(parentFolder).filePath(
        QString::fromUtf8(u8"飞船AI斗地主音效包模板"));
    if (!QDir().mkpath(templatePath)) {
        if (error) *error = QString::fromUtf8(u8"无法创建音效包模板目录");
        return false;
    }

    QStringList lines;
    lines << QString::fromUtf8(u8"飞船AI斗地主单机版音效包模板")
          << QStringLiteral("================================")
          << QString()
          << QString::fromUtf8(u8"请把未压缩 PCM WAV 文件放入下列相对路径。可以只提供想替换的部分文件。")
          << QString();
    QSet<QString> createdDirectories;
    for (const auto& entry : soundCatalog()) {
        const QString directory = QFileInfo(QDir(templatePath).filePath(entry.relativePath))
                                      .absolutePath();
        if (!createdDirectories.contains(directory)) {
            QDir().mkpath(directory);
            createdDirectories.insert(directory);
        }
        lines << soundCategoryDisplayName(entry.category) + QStringLiteral(" | ") +
                 entry.displayName + QStringLiteral(" | ") + entry.relativePath +
                 QStringLiteral(" | ") + entry.description;
    }

    QSaveFile file(QDir(templatePath).filePath(QString::fromUtf8(u8"音效文件对应说明.txt")));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QString::fromUtf8(u8"无法创建模板说明文件");
        return false;
    }
    const QByteArray bom = QByteArray::fromHex("EFBBBF");
    const QByteArray body = lines.join(QStringLiteral("\r\n")).toUtf8();
    if (file.write(bom) != bom.size() || file.write(body) != body.size() || !file.commit()) {
        if (error) *error = QString::fromUtf8(u8"无法保存模板说明文件");
        return false;
    }
    if (exportedPath) *exportedPath = templatePath;
    return true;
}

bool SoundOverrideManager::exportCurrentPack(const QString& parentFolder,
                                             QString* exportedPath, QString* error) const {
    QDir parent(parentFolder);
    if (!parent.exists()) {
        if (error) *error = QString::fromUtf8(u8"所选导出目录不存在");
        return false;
    }

    QTemporaryDir staging(parent.filePath(QStringLiteral(".fpdz-sound-export-XXXXXX")));
    if (!staging.isValid()) {
        if (error) *error = QString::fromUtf8(u8"无法创建完整音效包临时目录");
        return false;
    }

    QStringList lines;
    lines << QString::fromUtf8(u8"飞船AI斗地主单机版当前完整音效包")
          << QStringLiteral("====================================")
          << QString()
          << QString::fromUtf8(u8"本音效包包含导出时实际生效的全部 WAV，可通过“导入音效包”重新使用。")
          << QString();

    QSet<QString> processed;
    for (const auto& entry : soundCatalog()) {
        const QString relativePath = QDir::fromNativeSeparators(entry.relativePath);
        if (processed.contains(relativePath)) continue;
        processed.insert(relativePath);

        const QString sourcePath = m_soundService.resolvedSoundPath(relativePath);
        QString validationError;
        if (sourcePath.isEmpty() ||
            !SoundService::validateWaveFile(sourcePath, &validationError)) {
            if (error) {
                *error = QString::fromUtf8(u8"无法导出音效“%1”：%2")
                    .arg(relativePath, validationError);
            }
            return false;
        }

        QFile source(sourcePath);
        if (!source.open(QIODevice::ReadOnly)) {
            if (error) *error = QString::fromUtf8(u8"无法读取当前音效：") + relativePath;
            return false;
        }
        const QByteArray data = source.readAll();
        const QString targetPath = QDir(staging.path()).filePath(relativePath);
        if (!QDir().mkpath(QFileInfo(targetPath).absolutePath())) {
            if (error) *error = QString::fromUtf8(u8"无法创建完整音效包目录：") + relativePath;
            return false;
        }
        QSaveFile target(targetPath);
        if (!target.open(QIODevice::WriteOnly) || target.write(data) != data.size() ||
            !target.commit()) {
            if (error) *error = QString::fromUtf8(u8"无法导出当前音效：") + relativePath;
            return false;
        }
        lines << relativePath;
    }

    QSaveFile description(QDir(staging.path()).filePath(
        QString::fromUtf8(u8"音效文件对应说明.txt")));
    const QByteArray bom = QByteArray::fromHex("EFBBBF");
    const QByteArray body = lines.join(QStringLiteral("\r\n")).toUtf8();
    if (!description.open(QIODevice::WriteOnly) ||
        description.write(bom) != bom.size() ||
        description.write(body) != body.size() || !description.commit()) {
        if (error) *error = QString::fromUtf8(u8"无法保存完整音效包说明文件");
        return false;
    }

    const QString baseName = QString::fromUtf8(u8"飞船AI斗地主当前完整音效包-") +
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString finalPath = parent.filePath(baseName);
    int suffix = 2;
    while (QFileInfo::exists(finalPath)) {
        finalPath = parent.filePath(baseName + QStringLiteral("-%1").arg(suffix++));
    }
    if (!QDir().rename(staging.path(), finalPath)) {
        if (error) *error = QString::fromUtf8(u8"无法完成完整音效包导出");
        return false;
    }
    staging.setAutoRemove(false);
    if (exportedPath) *exportedPath = finalPath;
    qInfo() << "SoundOverrideManager: exported current pack files" << processed.size();
    return true;
}

} // namespace fpdz
