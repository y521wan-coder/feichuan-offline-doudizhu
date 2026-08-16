#include "data_paths.h"
#include <QDir>
#include <QFile>
namespace fpdz {
QString DataPaths::appDataDir() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}
QString DataPaths::savesDir() { return appDataDir() + "/saves"; }
QString DataPaths::logsDir() { return appDataDir() + "/logs"; }
QString DataPaths::replaysDir() { return appDataDir() + "/replays"; }
QString DataPaths::customSoundsDir() { return appDataDir() + "/custom_sounds"; }
QString DataPaths::modelsDir() { return appDataDir() + "/models"; }
QString DataPaths::masterModelsDir() { return modelsDir() + "/master"; }
QString DataPaths::activeMasterModelFile() {
    return masterModelsDir() + "/active_model.fpdzmodel";
}
QString DataPaths::tierSetsDir() { return modelsDir() + "/tier_sets"; }
QString DataPaths::activeTierManifestFile() { return modelsDir() + "/active_tiers.json"; }
QString DataPaths::modelInstallExitRequestFile() {
    return modelsDir() + "/training_install_exit_request.json";
}
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
    QDir().mkpath(masterModelsDir());
    QDir().mkpath(tierSetsDir());
}
} // namespace fpdz
