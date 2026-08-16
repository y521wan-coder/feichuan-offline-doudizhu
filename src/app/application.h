#pragma once
#include <QApplication>
#include <memory>
namespace fpdz {
class ServiceRegistry;
class Application : public QApplication {
public:
    Application(int& argc, char** argv);
    ~Application() override;
    ServiceRegistry& services();
    bool initialize();
private:
    std::unique_ptr<ServiceRegistry> m_services;
};
} // namespace fpdz
