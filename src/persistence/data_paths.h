#pragma once
#include <string>
#include <QStandardPaths>
namespace fpdz {
class DataPaths {
public:
    static QString appDataDir();
    static QString savesDir();
    static QString logsDir();
    static QString replaysDir();
    static QString customSoundsDir();
    static QString modelsDir();
    static QString masterModelsDir();
    static QString activeMasterModelFile();
    static QString tierSetsDir();
    static QString activeTierManifestFile();
    static QString modelInstallExitRequestFile();
    static QString settingsFile();
    static QString statisticsFile();
    static QString autoSaveFile();
    static QString autoSaveBackupFile();
    static void ensureDirectories();
};
} // namespace fpdz
