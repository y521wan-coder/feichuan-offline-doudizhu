#include "ai_service.h"

#include <QCoreApplication>
#include <QFile>
#include <QStandardPaths>
#include <QCommandLineParser>
#include <QTextStream>
#include <QThread>

#include <cstdio>
#include <iostream>
#include <string>

namespace {

class StdinReader final : public QThread {
    Q_OBJECT
public:
    using QThread::QThread;
signals:
    void lineRead(const QByteArray& line);
protected:
    void run() override {
        constexpr std::size_t maximumLineBytes = 128 * 1024;
        QByteArray line;
        line.reserve(static_cast<qsizetype>(maximumLineBytes + 1));
        bool oversized = false;
        char character = 0;
        while (std::cin.get(character)) {
            if (character == '\n') {
                emit lineRead(line);
                line.clear();
                oversized = false;
                continue;
            }
            if (character == '\r') continue;
            if (!oversized && static_cast<std::size_t>(line.size()) <= maximumLineBytes) {
                line.append(character);
                if (static_cast<std::size_t>(line.size()) > maximumLineBytes) oversized = true;
            }
        }
        if (!line.isEmpty()) emit lineRead(line);
    }
};

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("feichuan_offline_doudizhu"));
    app.setOrganizationName(QStringLiteral("Feichuan"));
    QCommandLineParser parser;
    parser.addOption({QStringLiteral("credentials-file"),
                      QStringLiteral("Override credential storage path for automated tests"),
                      QStringLiteral("path")});
    parser.process(app);
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                            QStringLiteral("/ai_battle");
    const QString credentialPath = parser.isSet(QStringLiteral("credentials-file"))
        ? parser.value(QStringLiteral("credentials-file"))
        : dataDir + QStringLiteral("/credentials.dat");
    fpdz::ai_service::AiService service(credentialPath);
    StdinReader reader;
    QObject::connect(&reader, &StdinReader::lineRead, &service,
                     &fpdz::ai_service::AiService::handleLine, Qt::QueuedConnection);
    QObject::connect(&service, &fpdz::ai_service::AiService::outputLine,
                     &service, [](const QByteArray& line) {
        std::fwrite(line.constData(), 1, static_cast<std::size_t>(line.size()), stdout);
        std::fflush(stdout);
    });
    QObject::connect(&reader, &QThread::finished, &app, &QCoreApplication::quit);
    service.start();
    reader.start();
    const int result = app.exec();
    reader.requestInterruption();
    reader.wait(100);
    return result;
}

#include "main.moc"
