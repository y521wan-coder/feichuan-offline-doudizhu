#include "save_repository.h"
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>

namespace fpdz {

bool SaveRepository::saveGame(const QString& path, const GameState& state) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    
    QJsonObject json = state.toJson();
    json["schemaVersion"] = 1;
    json["saveTime"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    
    QJsonDocument doc(json);
    file.write(doc.toJson(QJsonDocument::Indented));
    return file.commit();
}

bool SaveRepository::loadGame(const QString& path, GameState& state) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isNull()) return false;
    
    int schemaVersion = doc.object()["schemaVersion"].toInt(0);
    if (schemaVersion != 1) return false; // 版本不兼容
    
    state = GameState::fromJson(doc.object());
    return true;
}

bool SaveRepository::hasAutoSave(const QString& path) const {
    return QFile::exists(path);
}

bool SaveRepository::deleteSave(const QString& path) {
    return QFile::remove(path);
}

} // namespace fpdz
