#pragma once

#include <QStandardPaths>
#include <QString>

namespace fpdz {

class DataPaths {
public:
    static QString appDataDir();
    static QString savesDir();
    static QString logsDir();
    static QString replaysDir();
    static QString customSoundsDir();
    static QString settingsFile();
    static QString statisticsFile();
    static QString autoSaveFile();
    static QString autoSaveBackupFile();
    static QString aiBattleDir();
    static QString aiBattleSettingsFile();
    static QString aiBattleCredentialsFile();
    static QString aiBattleStatisticsFile();
    static QString aiBattleLogsDir();
    static QString aiBattleReplaysDir();
    static void ensureDirectories();
};

} // namespace fpdz
