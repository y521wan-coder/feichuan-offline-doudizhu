#include "app/application.h"
#include "app/service_registry.h"
#include "app/startup_controller.h"
#include <QStyleFactory>

int main(int argc, char* argv[]) {
    fpdz::Application app(argc, argv);

    if (!app.initialize()) {
        return 1;
    }

    fpdz::StartupController startup(app.services());
    startup.showModeSelection();

    return app.exec();
}
