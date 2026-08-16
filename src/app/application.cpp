#include "application.h"
#include "service_registry.h"
#include "../persistence/data_paths.h"
namespace fpdz {
Application::Application(int& argc, char** argv) : QApplication(argc, argv) {
    setApplicationName("FourPlayerDoudizhu");
    setApplicationDisplayName(QString::fromUtf8(u8"飞船AI斗地主单机版"));
    setApplicationVersion(FPDZ_APP_VERSION);
    setOrganizationName("FourPlayerDoudizhu");
}
Application::~Application() = default;
ServiceRegistry& Application::services() { return *m_services; }
bool Application::initialize() {
    DataPaths::ensureDirectories();
    m_services = std::make_unique<ServiceRegistry>();
    return m_services->initialize();
}
} // namespace fpdz
