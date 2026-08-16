#include "log_service.h"
#include <QFile>
#include <QTextStream>
#include <QDateTime>
namespace fpdz {
void LogService::init(const QString& logDir) { m_logDir = logDir; }
void LogService::info(const QString& message) {
    QFile file(m_logDir + "/app.log");
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out << QDateTime::currentDateTime().toString(Qt::ISODate) << " [INFO] " << message << "\n";
    }
}
void LogService::warning(const QString& message) {
    QFile file(m_logDir + "/app.log");
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out << QDateTime::currentDateTime().toString(Qt::ISODate) << "[WARN] " << message << "\n";
    }
}
void LogService::error(const QString& message) {
    QFile file(m_logDir + "/app.log");
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out << QDateTime::currentDateTime().toString(Qt::ISODate) << " [ERROR] " << message << "\n";
    }
}
} // namespace fpdz
