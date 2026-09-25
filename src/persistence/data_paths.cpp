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
QString DataPaths::aiBattleDir() { return appDataDir() + "/ai_battle"; }
QString DataPaths::aiBattleSettingsFile() { return aiBattleDir() + "/settings.json"; }
QString DataPaths::aiBattleCredentialsFile() { return aiBattleDir() + "/credentials.dat"; }
QString DataPaths::aiBattleStatisticsFile() { return aiBattleDir() + "/statistics.json"; }
QString DataPaths::aiBattleLogsDir() { return aiBattleDir() + "/logs"; }
QString DataPaths::aiBattleReplaysDir() { return aiBattleDir() + "/replays"; }

void DataPaths::ensureDirectories() {
    QDir().mkpath(appDataDir());
    QDir().mkpath(savesDir());
    QDir().mkpath(logsDir());
    QDir().mkpath(replaysDir());
    QDir().mkpath(customSoundsDir());
    QDir().mkpath(aiBattleDir());
    QDir().mkpath(aiBattleLogsDir());
    QDir().mkpath(aiBattleReplaysDir());
}

} // namespace fpdz
