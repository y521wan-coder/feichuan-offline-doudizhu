#include "statistics_repository.h"
#include <QFile>
#include <algorithm>
#include <QJsonDocument>
namespace fpdz {
bool StatisticsRepository::load(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    m_data = QJsonDocument::fromJson(file.readAll()).object();
    return true;
}
bool StatisticsRepository::save(const QString& path) const {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(m_data).toJson());
    return true;
}

void StatisticsRepository::recordRound(const RoundResult& result, Role humanRole) {
    if (!result.valid) return;
    const bool humanWon = result.landlordWon
        ? humanRole == Role::Landlord
        : humanRole == Role::Farmer;
    m_data["schemaVersion"] = 1;
    m_data["gamesPlayed"] = m_data["gamesPlayed"].toInt() + 1;
    m_data[humanWon ? "wins" : "losses"] =
        m_data[humanWon ? "wins" : "losses"].toInt() + 1;
    m_data[humanRole == Role::Landlord ? "landlordGames" : "farmerGames"] =
        m_data[humanRole == Role::Landlord ? "landlordGames" : "farmerGames"].toInt() + 1;
    m_data["totalScore"] = static_cast<qint64>(
        m_data["totalScore"].toVariant().toLongLong() + result.scoreChanges[0]);
    m_data["highestMultiplier"] = static_cast<qint64>(std::max(
        m_data["highestMultiplier"].toVariant().toLongLong(), result.finalMultiplier));
}
} // namespace fpdz
