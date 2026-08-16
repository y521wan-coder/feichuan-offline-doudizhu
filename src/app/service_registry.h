#pragma once
#include <memory>
#include <string>
namespace fpdz {
class GameEngine;
class AccessibilityService;
class LogService;
class DiagnosticTraceService;
class SettingsRepository;
class ServiceRegistry {
public:
    ServiceRegistry();
    ~ServiceRegistry();
    bool initialize();
    GameEngine& gameEngine();
    AccessibilityService& accessibility();
    LogService& logService();
    DiagnosticTraceService& diagnosticTrace();
private:
    std::unique_ptr<GameEngine> m_engine;
    std::unique_ptr<AccessibilityService> m_accessibility;
    std::unique_ptr<LogService> m_log;
    std::unique_ptr<DiagnosticTraceService> m_diagnosticTrace;
    std::unique_ptr<SettingsRepository> m_settings;
};
} // namespace fpdz
