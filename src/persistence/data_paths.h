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
    static void ensureDirectories();
};

} // namespace fpdz
