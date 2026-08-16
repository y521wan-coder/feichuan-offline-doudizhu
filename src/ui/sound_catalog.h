#pragma once

#include "../core/audio/sound_category.h"

#include <QString>
#include <QVector>

namespace fpdz {

struct SoundCatalogEntry {
    QString id;
    SoundCategory category = SoundCategory::StartupDeal;
    QString displayName;
    QString relativePath;
    QString description;
};

QString soundCategoryDisplayName(SoundCategory category);
QVector<SoundCategory> soundCategories();
const QVector<SoundCatalogEntry>& soundCatalog();
QVector<SoundCatalogEntry> soundCatalogEntries(SoundCategory category);
const SoundCatalogEntry* findSoundCatalogEntry(const QString& relativePath);

} // namespace fpdz
