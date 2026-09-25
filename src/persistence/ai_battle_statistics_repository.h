#pragma once

#include "../ai/ai_decision_request.h"
#include "../app/ai_battle_types.h"
#include "../core/model/game_snapshot.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace fpdz {

class AiBattleStatisticsRepository {
public:
    bool load(const QString& path);
    bool save(const QString& path) const;
    void recordRequest(int seat, const SeatControllerConfig& controller,
                       bool success, const QString& errorCode, int latencyMilliseconds,
                       qint64 inputTokens, qint64 outputTokens);
    bool recordGameStarted(const QString& directory, const FullGameState& state,
                           const AiBattleSettings& settings);
    void recordPublicAction(const FullGameState& state);
    bool recordDecisionStarted(const QString& directory,
                               const AiDecisionRequest& request,
                               qsizetype requestBytes);
    bool recordDecisionFinished(const QString& directory,
                                const AiDecisionRequest& request,
                                bool success, const QString& outcome,
                                const QString& errorCode, int latencyMilliseconds,
                                qint64 inputTokens, qint64 outputTokens,
                                int actionId = -1, qint64 executedActionSequence = -1);
    void recordRound(const RoundResult& result, const FullGameState& state,
                     const AiBattleSettings& settings);
    bool recordRoundFinished(const QString& directory, const RoundResult& result,
                             const FullGameState& state,
                             const AiBattleSettings& settings);
    bool saveReplaySummary(const QString& directory, const RoundResult& result,
                           const FullGameState& state,
                           const AiBattleSettings& settings) const;
    bool saveDetailedGame(const QString& directory, const RoundResult& result,
                          const FullGameState& state, const AiBattleSettings& settings);
    QByteArray detailedGamesForCopy(const QString& directory) const;
    QJsonObject data() const { return m_data; }

private:
    QString tracePath(const QString& directory, uint64_t gameId);
    bool appendTraceEvent(const QString& directory, uint64_t gameId,
                          QJsonObject event);

    QJsonObject m_data;
    QHash<qulonglong, QString> m_tracePaths;
    QHash<qulonglong, QString> m_detailedPaths;
    QHash<qulonglong, QString> m_gameStartedAt;
    QHash<qulonglong, QJsonArray> m_detailedDecisions;
    QHash<qulonglong, QHash<qulonglong, QJsonObject>> m_detailedActionContexts;
    quint64 m_detailedSequence = 0;
};

} // namespace fpdz
