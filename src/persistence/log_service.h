#pragma once
#include <QString>
namespace fpdz {
class LogService {
public:
    void init(const QString& logDir);
    void info(const QString& message);
    void warning(const QString& message);
    void error(const QString& message);
private:
    QString m_logDir;
};
} // namespace fpdz
