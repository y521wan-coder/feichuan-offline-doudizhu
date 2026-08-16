#include "service_registry.h"
#include "../core/engine/game_engine.h"
#include "../accessibility/accessibility_service.h"
#include "../persistence/log_service.h"
#include "../persistence/settings_repository.h"
#include "../persistence/data_paths.h"
#include <persistence/diagnostic_trace_service.h>
namespace fpdz {
ServiceRegistry::ServiceRegistry() = default;
ServiceRegistry::~ServiceRegistry() = default;
bool ServiceRegistry::initialize() {
    m_engine = std::make_unique<GameEngine>();
    m_accessibility = std::make_unique<AccessibilityService>();
    m_log = std::make_unique<LogService>();
    m_diagnosticTrace = std::make_unique<DiagnosticTraceService>();
    m_settings = std::make_unique<SettingsRepository>();
    m_log->init(DataPaths::logsDir());
    m_diagnosticTrace->init(DataPaths::logsDir());
    m_log->info("服务注册表初始化完成");
    return true;
}
GameEngine& ServiceRegistry::gameEngine() { return *m_engine; }
AccessibilityService& ServiceRegistry::accessibility() { return *m_accessibility; }
LogService& ServiceRegistry::logService() { return *m_log; }
DiagnosticTraceService& ServiceRegistry::diagnosticTrace() { return *m_diagnosticTrace; }
} // namespace fpdz
