#include "online_window.h"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("feichuan_offline_doudizhu"));
    QCoreApplication::setApplicationName(QStringLiteral("FeichuanOnlineDoudizhu"));
    fpdz::OnlineWindow window;
    window.show();
    return app.exec();
}

