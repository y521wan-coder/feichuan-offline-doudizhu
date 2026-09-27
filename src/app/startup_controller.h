#pragma once

#include "game_mode.h"

#include <QObject>
#include <QProcess>
#include <memory>

namespace fpdz {

class ServiceRegistry;
class GameEngine;
class MainWindow;
class ModeSelectionWindow;
class DiagnosticTraceService;

class StartupController final : public QObject {
    Q_OBJECT
public:
    explicit StartupController(ServiceRegistry& services, QObject* parent = nullptr);
    ~StartupController() override;
    void showModeSelection();

private:
    void startMode(GameMode mode);
    void startOnlineMode();
    void finishOnlineMode(const QString& error = {});
    void returnToModeSelection();

    ServiceRegistry& m_services;
    std::unique_ptr<GameEngine> m_sessionEngine;
    std::unique_ptr<ModeSelectionWindow> m_modeSelection;
    std::unique_ptr<MainWindow> m_mainWindow;
    std::unique_ptr<DiagnosticTraceService> m_aiBattleDiagnosticTrace;
    QProcess m_onlineProcess;
    bool m_onlineModeActive = false;
    bool m_previousQuitOnLastWindowClosed = true;
};

} // namespace fpdz
