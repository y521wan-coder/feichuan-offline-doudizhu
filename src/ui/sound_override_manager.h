#pragma once

#include "sound_catalog.h"

#include <QStringList>
#include <QVector>

namespace fpdz {

class SoundService;

struct SoundPackFile {
    SoundCatalogEntry entry;
    QString sourcePath;
};

struct SoundPackAnalysis {
    QVector<SoundPackFile> files;
    QStringList invalidFiles;
    QStringList unknownFiles;
};

class SoundOverrideManager {
public:
    explicit SoundOverrideManager(SoundService& soundService);

    QString overridePath(const QString& relativePath) const;
    bool hasOverride(const QString& relativePath) const;
    bool replaceSound(const SoundCatalogEntry& entry, const QString& sourcePath,
                      QString* error = nullptr);
    bool restoreSound(const SoundCatalogEntry& entry, QString* error = nullptr);
    int restoreCategory(SoundCategory category, QString* error = nullptr);
    int restoreAll(QString* error = nullptr);

    SoundPackAnalysis analyzePack(const QString& folderPath) const;
    bool importPack(const SoundPackAnalysis& analysis, QString* error = nullptr);
    bool exportTemplate(const QString& parentFolder, QString* exportedPath,
                        QString* error = nullptr) const;
    bool exportCurrentPack(const QString& parentFolder, QString* exportedPath,
                           QString* error = nullptr) const;

private:
    bool copyValidatedWave(const QString& sourcePath, const QString& relativePath,
                           QString* error);

    SoundService& m_soundService;
};

} // namespace fpdz
