#pragma once
#include <QString>
#include <QJsonObject>
#include "../core/model/game_snapshot.h"
namespace fpdz {
class StatisticsRepository {
public:
    bool load(const QString& path);
    bool save(const QString& path) const;
    void recordRound(const RoundResult& result, Role humanRole);
    QJsonObject data() const { return m_data; }
private:
    QJsonObject m_data;
};
} // namespace fpdz
