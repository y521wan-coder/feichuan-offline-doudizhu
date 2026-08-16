#pragma once
#include <QString>
#include <QJsonObject>
namespace fpdz {
class SettingsRepository {
public:
    bool load(const QString& path);
    bool save(const QString& path) const;
    QJsonObject data() const { return m_data; }
    void setData(const QJsonObject& data) { m_data = data; }
private:
    QJsonObject m_data;
};
} // namespace fpdz
