#include "replay_repository.h"
#include <QFile>
namespace fpdz {
bool ReplayRepository::saveReplay(const QString& path, const QString& data) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(data.toUtf8());
    return true;
}
bool ReplayRepository::loadReplay(const QString& path, QString& data) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    data = QString::fromUtf8(file.readAll());
    return true;
}
} // namespace fpdz
