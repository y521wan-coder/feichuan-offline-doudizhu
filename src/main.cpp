#include "app/application.h"
#include "app/service_registry.h"
#include "ui/main_window.h"
#include <QStyleFactory>

int main(int argc, char* argv[]) {
    fpdz::Application app(argc, argv);

    if (!app.initialize()) {
        return 1;
    }

    auto window = fpdz::createMainWindow(app.services().gameEngine(),
                                         app.services().accessibility(),
                                         &app.services().diagnosticTrace());
    window->show();

    return app.exec();
}
