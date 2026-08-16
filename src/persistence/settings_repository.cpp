#include "settings_repository.h"
#include <QFile>
#include <QJsonDocument>
namespace fpdz {
bool SettingsRepository::load(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isNull()) return false;
    m_data = doc.object();
    return true;
}
bool SettingsRepository::save(const QString& path) const {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(m_data).toJson());
    return true;
}
} // namespace fpdz
