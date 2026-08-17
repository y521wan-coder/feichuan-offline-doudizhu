#include "data_paths.h"

#include <QDir>

namespace fpdz {

QString DataPaths::appDataDir() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}
QString DataPaths::savesDir() { return appDataDir() + "/saves"; }
QString DataPaths::logsDir() { return appDataDir() + "/logs"; }
QString DataPaths::replaysDir() { return appDataDir() + "/replays"; }
QString DataPaths::customSoundsDir() { return appDataDir() + "/custom_sounds"; }
QString DataPaths::settingsFile() { return appDataDir() + "/settings.json"; }
QString DataPaths::statisticsFile() { return appDataDir() + "/statistics.json"; }
QString DataPaths::autoSaveFile() { return savesDir() + "/autosave.json"; }
QString DataPaths::autoSaveBackupFile() { return savesDir() + "/autosave.backup.json"; }

void DataPaths::ensureDirectories() {
    QDir().mkpath(appDataDir());
    QDir().mkpath(savesDir());
    QDir().mkpath(logsDir());
    QDir().mkpath(replaysDir());
    QDir().mkpath(customSoundsDir());
}

} // namespace fpdz
