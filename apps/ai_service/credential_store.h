#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace fpdz::ai_service {

class CredentialStore {
public:
    explicit CredentialStore(QString filePath);

    bool load(QString* error = nullptr);
    bool save(QString* error = nullptr) const;
    QJsonArray publicRecords() const;
    QJsonObject record(const QString& id) const;
    bool upsert(QJsonObject record, bool clearSecret, QString* id,
                QString* error = nullptr);
    bool remove(const QString& id, QString* error = nullptr);

    static bool headerNameAllowed(const QString& name);

private:
    QString m_filePath;
    QJsonArray m_records;
};

} // namespace fpdz::ai_service
