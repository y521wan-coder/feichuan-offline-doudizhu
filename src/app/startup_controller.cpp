#include "startup_controller.h"

#include "service_registry.h"
#include "../core/engine/game_engine.h"
#include "../persistence/data_paths.h"
#include "../persistence/diagnostic_trace_service.h"
#include "../ui/main_window.h"
#include "../ui/mode_selection_window.h"

#include <QTimer>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>

namespace fpdz {

StartupController::StartupController(ServiceRegistry& services, QObject* parent)
    : QObject(parent), m_services(services) {
    connect(&m_onlineProcess, &QProcess::started, this, [this]() {
        if (m_onlineModeActive && m_modeSelection) m_modeSelection->hide();
    });
    connect(&m_onlineProcess, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (!m_onlineModeActive) return;
        if (error == QProcess::FailedToStart) {
            finishOnlineMode(QString::fromUtf8(u8"在线程序启动失败：%1")
                                 .arg(m_onlineProcess.errorString()));
        }
    });
    connect(&m_onlineProcess,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus status) {
        if (!m_onlineModeActive) return;
        finishOnlineMode(status == QProcess::CrashExit
                             ? QString::fromUtf8(u8"在线程序意外退出，请复制在线程序诊断信息反馈。")
                             : exitCode != 0
                                   ? QString::fromUtf8(u8"在线程序启动或运行失败，退出码 %1。")
                                         .arg(exitCode)
                                   : QString());
    });
}

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
    if (mode == GameMode::Online) {
        startOnlineMode();
        return;
    }
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

void StartupController::startOnlineMode() {
    if (m_onlineModeActive) return;
    const QString executable = QDir(QCoreApplication::applicationDirPath()).filePath(
        QString::fromUtf8(u8"飞船斗地主在线版.exe"));
    if (!QFileInfo::exists(executable)) {
        QMessageBox::warning(m_modeSelection.get(),
                             QString::fromUtf8(u8"在线真人版不可用"),
                             QString::fromUtf8(u8"未找到在线程序。请使用现有更新器修复安装。离线单机版和AI对战模式仍可使用。"));
        return;
    }
    m_previousQuitOnLastWindowClosed = QApplication::quitOnLastWindowClosed();
    QApplication::setQuitOnLastWindowClosed(false);
    m_onlineModeActive = true;
    m_onlineProcess.setWorkingDirectory(QCoreApplication::applicationDirPath());
    m_onlineProcess.start(executable);
}

void StartupController::finishOnlineMode(const QString& error) {
    if (!m_onlineModeActive) return;
    m_onlineModeActive = false;
    showModeSelection();
    QApplication::setQuitOnLastWindowClosed(m_previousQuitOnLastWindowClosed);
    if (!error.isEmpty()) {
        QMessageBox::warning(m_modeSelection.get(),
                             QString::fromUtf8(u8"在线真人版"), error);
    }
}

void StartupController::returnToModeSelection() {
    if (m_mainWindow) m_mainWindow->hide();
    QTimer::singleShot(0, this, [this]() { showModeSelection(); });
}

} // namespace fpdz
