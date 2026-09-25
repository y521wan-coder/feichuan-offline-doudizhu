#include "startup_controller.h"

#include "service_registry.h"
#include "../core/engine/game_engine.h"
#include "../persistence/data_paths.h"
#include "../persistence/diagnostic_trace_service.h"
#include "../ui/main_window.h"
#include "../ui/mode_selection_window.h"

#include <QTimer>

namespace fpdz {

StartupController::StartupController(ServiceRegistry& services, QObject* parent)
    : QObject(parent), m_services(services) {}

StartupController::~StartupController() = default;

void StartupController::showModeSelection() {
    m_mainWindow.reset();
    m_aiBattleDiagnosticTrace.reset();
    m_sessionEngine.reset();
    m_modeSelection = std::make_unique<ModeSelectionWindow>();
    connect(m_modeSelection.get(), &ModeSelectionWindow::modeSelected,
            this, &StartupController::startMode);
    m_modeSelection->show();
    m_modeSelection->raise();
    m_modeSelection->activateWindow();
}

void StartupController::startMode(GameMode mode) {
    if (m_modeSelection) m_modeSelection->hide();
    m_sessionEngine = std::make_unique<GameEngine>();
    DiagnosticTraceService* trace = &m_services.diagnosticTrace();
    if (mode == GameMode::AiBattle) {
        m_aiBattleDiagnosticTrace = std::make_unique<DiagnosticTraceService>();
        m_aiBattleDiagnosticTrace->init(DataPaths::aiBattleLogsDir());
        trace = m_aiBattleDiagnosticTrace.get();
    }
    m_mainWindow = createMainWindow(*m_sessionEngine, m_services.accessibility(),
                                    trace, mode);
    connect(m_mainWindow.get(), &MainWindow::returnToModeSelectionRequested,
            this, &StartupController::returnToModeSelection);
    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();
    m_modeSelection.reset();
}

void StartupController::returnToModeSelection() {
    if (m_mainWindow) m_mainWindow->hide();
    QTimer::singleShot(0, this, [this]() { showModeSelection(); });
}

} // namespace fpdz
