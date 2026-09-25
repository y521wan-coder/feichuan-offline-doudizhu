#include "ai_battle_statistics_repository.h"
#include "../core/rules/pattern_analyzer.h"
#include "../core/text/card_text_formatter.h"
#include "../app/ai_battle_prompt.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QDir>
#include <QUuid>

namespace fpdz {
namespace {

void appendBounded(QJsonObject& data, const QString& key, const QJsonObject& entry) {
    QJsonArray array = data.value(key).toArray();
    array.append(entry);
    while (array.size() > 1000) array.removeFirst();
    data[key] = array;
}

int landlordSeat(const FullGameState& state) {
    for (int index = 0; index < state.activePlayerCount; ++index) {
        if (state.players[static_cast<std::size_t>(index)].role == Role::Landlord) {
            return index;
        }
    }
    return -1;
}

QJsonArray controllerSummary(const FullGameState& state,
                             const AiBattleSettings& settings,
                             const RoundResult& result) {
    QJsonArray seats;
    for (int index = 0; index < state.activePlayerCount; ++index) {
        QJsonObject seat{{QStringLiteral("seat"), index},
                         {QStringLiteral("score"), result.scoreChanges[index]}};
        if (index == 0) {
            seat[QStringLiteral("controller")] = QStringLiteral("human");
        } else {
            const auto& controller = settings.seats[static_cast<std::size_t>(index)];
            seat[QStringLiteral("controller")] = controller.kind == SeatControllerKind::CloudAi
                ? QStringLiteral("cloud") : QStringLiteral("local");
            if (controller.kind == SeatControllerKind::CloudAi) {
                seat[QStringLiteral("model")] = controller.model;
                seat[QStringLiteral("strength")] = static_cast<int>(controller.strength);
                seat[QStringLiteral("timeoutSeconds")] = controller.timeoutSeconds;
            } else {
                seat[QStringLiteral("difficulty")] = static_cast<int>(controller.localDifficulty);
            }
        }
        seats.append(seat);
    }
    return seats;
}

QString phaseName(GamePhase phase) {
    if (phase == GamePhase::Bidding) return QStringLiteral("bidding");
    if (phase == GamePhase::Playing) return QStringLiteral("playing");
    return QStringLiteral("other");
}

QJsonObject requestTraceContext(const AiDecisionRequest& request) {
    const QJsonObject publicState = request.observation;
    return {
        {QStringLiteral("requestId"), request.requestId},
        {QStringLiteral("gameId"), static_cast<qint64>(request.gameId)},
        {QStringLiteral("eventSequence"), static_cast<qint64>(request.eventSequence)},
        {QStringLiteral("phase"), phaseName(request.phase)},
        {QStringLiteral("seat"), static_cast<int>(request.playerId)},
        {QStringLiteral("model"), request.controller.model},
        {QStringLiteral("strength"), static_cast<int>(request.controller.strength)},
        {QStringLiteral("timeoutSeconds"), request.controller.timeoutSeconds},
        {QStringLiteral("legalActionCount"), request.actions.size()},
        {QStringLiteral("playerCount"), publicState.value("player_count")},
        {QStringLiteral("highestBid"), publicState.value("highest_bid")},
        {QStringLiteral("baseScore"), publicState.value("base_score")},
        {QStringLiteral("multiplier"), publicState.value("multiplier")},
        {QStringLiteral("landlord"), publicState.value("landlord")},
        {QStringLiteral("remainingCardCounts"), publicState.value("remaining_cards")},
        {QStringLiteral("lastPlayedBy"), publicState.value("last_played_by")},
        {QStringLiteral("consecutivePasses"), publicState.value("consecutive_passes")},
        {QStringLiteral("publicHistoryCount"),
         publicState.value("history").toArray().size()}
    };
}

QJsonArray publicCards(const std::vector<Card>& cards) {
    QJsonArray result;
    for (const Card& card : cards) {
        result.append(QJsonObject{
            {QStringLiteral("id"), static_cast<int>(card.id())},
            {QStringLiteral("name"), QString::fromStdWString(card.displayName())}});
    }
    return result;
}

} // namespace

bool AiBattleStatisticsRepository::load(const QString& path) {
    QFile file(path);
    if (!file.exists()) {
        m_data = {{QStringLiteral("schemaVersion"), 1}};
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) return false;
    const auto document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) return false;
    m_data = document.object();
    return true;
}

bool AiBattleStatisticsRepository::save(const QString& path) const {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QByteArray bytes = QJsonDocument(m_data).toJson(QJsonDocument::Indented);
    return file.write(bytes) == bytes.size() && file.commit();
}

void AiBattleStatisticsRepository::recordRequest(
    int seat, const SeatControllerConfig& controller, bool success,
    const QString& errorCode, int latencyMilliseconds, qint64 inputTokens,
    qint64 outputTokens) {
    m_data[QStringLiteral("schemaVersion")] = 1;
    m_data[QStringLiteral("requestCount")] =
        m_data.value("requestCount").toInteger() + 1;
    if (!success) {
        m_data[QStringLiteral("failureCount")] =
            m_data.value("failureCount").toInteger() + 1;
        QJsonObject failures = m_data.value("failureTypes").toObject();
        const QString safeCode = errorCode.isEmpty() ? QStringLiteral("unknown") : errorCode;
        failures[safeCode] = failures.value(safeCode).toInteger() + 1;
        m_data[QStringLiteral("failureTypes")] = failures;
    }
    QJsonObject entry{{QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                      {QStringLiteral("seat"), seat},
                      {QStringLiteral("model"), controller.model},
                      {QStringLiteral("strength"), static_cast<int>(controller.strength)},
                      {QStringLiteral("timeoutSeconds"), controller.timeoutSeconds},
                      {QStringLiteral("success"), success},
                      {QStringLiteral("latencyMilliseconds"), latencyMilliseconds}};
    if (!success) entry[QStringLiteral("errorCode")] = errorCode;
    if (inputTokens >= 0) entry[QStringLiteral("inputTokens")] = inputTokens;
    if (outputTokens >= 0) entry[QStringLiteral("outputTokens")] = outputTokens;
    appendBounded(m_data, QStringLiteral("requests"), entry);
}

QString AiBattleStatisticsRepository::tracePath(const QString& directory,
                                                uint64_t gameId) {
    const qulonglong key = static_cast<qulonglong>(gameId);
    const auto existing = m_tracePaths.constFind(key);
    if (existing != m_tracePaths.cend()) return existing.value();
    QDir().mkpath(directory);
    const QString suffix = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    const QString fileName = QStringLiteral("ai-game-%1-%2-%3.jsonl")
        .arg(gameId)
        .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmsszzz")))
        .arg(suffix);
    const QString path = QDir(directory).filePath(fileName);
    m_tracePaths.insert(key, path);
    return path;
}

bool AiBattleStatisticsRepository::appendTraceEvent(const QString& directory,
                                                    uint64_t gameId,
                                                    QJsonObject event) {
    event[QStringLiteral("schemaVersion")] = 1;
    event[QStringLiteral("time")] =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QFile file(tracePath(directory, gameId));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) return false;
    const QByteArray line = QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
    return file.write(line) == line.size();
}

bool AiBattleStatisticsRepository::recordGameStarted(
    const QString& directory, const FullGameState& state,
    const AiBattleSettings& settings) {
    m_gameStartedAt.clear();
    m_detailedDecisions.clear();
    m_detailedActionContexts.clear();
    m_detailedPaths.clear();
    m_gameStartedAt.insert(static_cast<qulonglong>(state.gameId),
                           QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    const RoundResult emptyResult;
    return appendTraceEvent(directory, state.gameId,
        {{QStringLiteral("event"), QStringLiteral("game_started")},
         {QStringLiteral("gameId"), static_cast<qint64>(state.gameId)},
         {QStringLiteral("playerCount"), state.activePlayerCount},
         {QStringLiteral("seats"), controllerSummary(state, settings, emptyResult)}});
}

void AiBattleStatisticsRepository::recordPublicAction(const FullGameState& state) {
    const qulonglong gameKey = static_cast<qulonglong>(state.gameId);
    if (!m_gameStartedAt.contains(gameKey)) {
        m_gameStartedAt.insert(gameKey,
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    }
    if (state.actionHistory.empty()) return;
    const PublicActionRecord& action = state.actionHistory.back();
    QJsonArray remaining;
    for (int index = 0; index < state.activePlayerCount; ++index) {
        remaining.append(state.players[static_cast<std::size_t>(index)].hand.size());
    }
    m_detailedActionContexts[gameKey].insert(
        static_cast<qulonglong>(action.sequence), QJsonObject{
            {QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {QStringLiteral("remainingCardCountsAfter"), remaining},
            {QStringLiteral("multiplierAfter"), static_cast<qint64>(state.currentMultiplier)}});
}

bool AiBattleStatisticsRepository::recordDecisionStarted(
    const QString& directory, const AiDecisionRequest& request,
    qsizetype requestBytes) {
    QJsonObject event = requestTraceContext(request);
    event[QStringLiteral("event")] = QStringLiteral("decision_started");
    event[QStringLiteral("requestBytes")] = static_cast<qint64>(requestBytes);
    QJsonObject detail = requestTraceContext(request);
    detail[QStringLiteral("startedAt")] =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    detail[QStringLiteral("requestBytes")] = static_cast<qint64>(requestBytes);
    detail[QStringLiteral("seat")] = static_cast<int>(request.playerId) + 1;
    for (const auto* key : {"landlord", "lastPlayedBy"}) {
        const int value = detail.value(QLatin1String(key)).toInt(-1);
        detail[QLatin1String(key)] = value < 0 ? -1 : value + 1;
    }
    detail[QStringLiteral("strategyPrompt")] = request.strategyPrompt.isEmpty()
        ? defaultAiStrategyPrompt() : request.strategyPrompt;
    m_detailedDecisions[static_cast<qulonglong>(request.gameId)].append(detail);
    return appendTraceEvent(directory, request.gameId, event);
}

bool AiBattleStatisticsRepository::recordDecisionFinished(
    const QString& directory, const AiDecisionRequest& request, bool success,
    const QString& outcome, const QString& errorCode, int latencyMilliseconds,
    qint64 inputTokens, qint64 outputTokens, int actionId,
    qint64 executedActionSequence) {
    QJsonObject event = requestTraceContext(request);
    event[QStringLiteral("event")] = QStringLiteral("decision_finished");
    event[QStringLiteral("success")] = success;
    event[QStringLiteral("outcome")] = outcome;
    event[QStringLiteral("latencyMilliseconds")] = latencyMilliseconds;
    if (!errorCode.isEmpty()) event[QStringLiteral("errorCode")] = errorCode;
    if (inputTokens >= 0) event[QStringLiteral("inputTokens")] = inputTokens;
    if (outputTokens >= 0) event[QStringLiteral("outputTokens")] = outputTokens;
    if (actionId >= 0) event[QStringLiteral("actionId")] = actionId;
    QJsonArray& decisions = m_detailedDecisions[static_cast<qulonglong>(request.gameId)];
    for (qsizetype index = decisions.size(); index > 0; --index) {
        QJsonObject detail = decisions.at(index - 1).toObject();
        if (detail.value(QStringLiteral("requestId")).toString() != request.requestId) continue;
        detail[QStringLiteral("finishedAt")] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        detail[QStringLiteral("success")] = success;
        detail[QStringLiteral("outcome")] = outcome;
        detail[QStringLiteral("latencyMilliseconds")] = latencyMilliseconds;
        detail[QStringLiteral("latencySeconds")] = latencyMilliseconds / 1000.0;
        if (!errorCode.isEmpty()) detail[QStringLiteral("errorCode")] = errorCode;
        if (inputTokens >= 0) detail[QStringLiteral("inputTokens")] = inputTokens;
        if (outputTokens >= 0) detail[QStringLiteral("outputTokens")] = outputTokens;
        if (actionId >= 0) detail[QStringLiteral("actionId")] = actionId;
        if (executedActionSequence >= 0) {
            detail[QStringLiteral("executedActionSequence")] = executedActionSequence;
        }
        decisions.replace(index - 1, detail);
        break;
    }
    return appendTraceEvent(directory, request.gameId, event);
}

void AiBattleStatisticsRepository::recordRound(const RoundResult& result,
                                                const FullGameState& state,
                                                const AiBattleSettings& settings) {
    if (!result.valid) return;
    const QJsonArray seats = controllerSummary(state, settings, result);
    QJsonObject round{{QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                      {QStringLiteral("playerCount"), state.activePlayerCount},
                      {QStringLiteral("winner"), static_cast<int>(result.winner)},
                      {QStringLiteral("landlord"), landlordSeat(state)},
                      {QStringLiteral("landlordWon"), result.landlordWon},
                      {QStringLiteral("finalMultiplier"), result.finalMultiplier},
                      {QStringLiteral("seats"), seats}};
    appendBounded(m_data, QStringLiteral("rounds"), round);
    m_data[QStringLiteral("gamesPlayed")] = m_data.value("gamesPlayed").toInteger() + 1;
}

bool AiBattleStatisticsRepository::recordRoundFinished(
    const QString& directory, const RoundResult& result, const FullGameState& state,
    const AiBattleSettings& settings) {
    if (!result.valid) return false;
    return appendTraceEvent(directory, state.gameId,
        {{QStringLiteral("event"), QStringLiteral("game_finished")},
         {QStringLiteral("gameId"), static_cast<qint64>(state.gameId)},
         {QStringLiteral("playerCount"), state.activePlayerCount},
         {QStringLiteral("winner"), static_cast<int>(result.winner)},
         {QStringLiteral("landlord"), landlordSeat(state)},
         {QStringLiteral("landlordWon"), result.landlordWon},
         {QStringLiteral("finalMultiplier"), result.finalMultiplier},
         {QStringLiteral("seats"), controllerSummary(state, settings, result)}});
}

bool AiBattleStatisticsRepository::saveReplaySummary(
    const QString& directory, const RoundResult& result, const FullGameState& state,
    const AiBattleSettings& settings) const {
    if (!result.valid) return false;
    QJsonArray history;
    for (const auto& action : state.actionHistory) {
        QJsonObject item{{QStringLiteral("type"), static_cast<int>(action.type)},
                         {QStringLiteral("seat"), static_cast<int>(action.playerId)},
                         {QStringLiteral("sequence"), static_cast<qint64>(action.sequence)}};
        if (action.type == PublicActionType::Bid) item[QStringLiteral("bid")] = action.bidValue;
        if (action.type == PublicActionType::Play) {
            item[QStringLiteral("cardCount")] = static_cast<int>(action.cards.size());
        }
        history.append(item);
    }
    const QJsonObject summary{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("gameId"), static_cast<qint64>(state.gameId)},
        {QStringLiteral("playerCount"), state.activePlayerCount},
        {QStringLiteral("winner"), static_cast<int>(result.winner)},
        {QStringLiteral("landlord"), landlordSeat(state)},
        {QStringLiteral("landlordWon"), result.landlordWon},
        {QStringLiteral("finalMultiplier"), result.finalMultiplier},
        {QStringLiteral("seats"), controllerSummary(state, settings, result)},
        {QStringLiteral("history"), history}};
    QDir().mkpath(directory);
    const QString fileName = QStringLiteral("game-%1-%2.json")
        .arg(state.gameId)
        .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmsszzz")));
    QSaveFile file(QDir(directory).filePath(fileName));
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QByteArray bytes = QJsonDocument(summary).toJson(QJsonDocument::Indented);
    return file.write(bytes) == bytes.size() && file.commit();
}

bool AiBattleStatisticsRepository::saveDetailedGame(
    const QString& directory, const RoundResult& result, const FullGameState& state,
    const AiBattleSettings& settings) {
    if (!result.valid) return false;
    QJsonArray detailedSeats = controllerSummary(state, settings, result);
    for (int index = 0; index < detailedSeats.size(); ++index) {
        QJsonObject seat = detailedSeats.at(index).toObject();
        seat[QStringLiteral("seat")] = index + 1;
        seat[QStringLiteral("name")] = QString::fromStdWString(
            state.players[static_cast<std::size_t>(index)].name);
        seat[QStringLiteral("role")] = state.players[static_cast<std::size_t>(index)].role == Role::Landlord
            ? QStringLiteral("landlord") : QStringLiteral("farmer");
        detailedSeats.replace(index, seat);
    }
    QJsonArray actions;
    for (const PublicActionRecord& action : state.actionHistory) {
        QJsonObject item{
            {QStringLiteral("sequence"), static_cast<qint64>(action.sequence)},
            {QStringLiteral("seat"), static_cast<int>(action.playerId) + 1}};
        if (action.type == PublicActionType::Bid) {
            item[QStringLiteral("type")] = QStringLiteral("bid");
            item[QStringLiteral("bid")] = action.bidValue;
        } else if (action.type == PublicActionType::Pass) {
            item[QStringLiteral("type")] = QStringLiteral("pass");
        } else {
            item[QStringLiteral("type")] = QStringLiteral("play");
            item[QStringLiteral("cards")] = publicCards(action.cards);
            const CardPattern pattern = PatternAnalyzer::analyze(action.cards, state.activePlayerCount);
            item[QStringLiteral("pattern")] = QString::fromStdWString(
                CardTextFormatter::formatPlayedCards(pattern, action.cards, state.activePlayerCount));
        }
        const QJsonObject context = m_detailedActionContexts
            .value(static_cast<qulonglong>(state.gameId))
            .value(static_cast<qulonglong>(action.sequence));
        for (auto it = context.constBegin(); it != context.constEnd(); ++it) {
            item.insert(it.key(), it.value());
        }
        actions.append(item);
    }
    QJsonObject detail{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("gameId"), static_cast<qint64>(state.gameId)},
        {QStringLiteral("startedAt"), m_gameStartedAt.value(static_cast<qulonglong>(state.gameId))},
        {QStringLiteral("finishedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("playerCount"), state.activePlayerCount},
        {QStringLiteral("landlordSeat"), landlordSeat(state) + 1},
        {QStringLiteral("winnerSeat"), static_cast<int>(result.winner) + 1},
        {QStringLiteral("landlordWon"), result.landlordWon},
        {QStringLiteral("baseScore"), state.baseScore},
        {QStringLiteral("finalMultiplier"), result.finalMultiplier},
        {QStringLiteral("spring"), result.spring},
        {QStringLiteral("antiSpring"), result.antiSpring},
        {QStringLiteral("seats"), detailedSeats},
        {QStringLiteral("actions"), actions},
        {QStringLiteral("cloudDecisions"), m_detailedDecisions.value(static_cast<qulonglong>(state.gameId))}};
    if (state.bottomCardsRevealed) {
        detail[QStringLiteral("revealedBottomCards")] = publicCards(state.bottomCards);
    }
    QDir().mkpath(directory);
    const qulonglong key = static_cast<qulonglong>(state.gameId);
    QString path = m_detailedPaths.value(key);
    if (path.isEmpty()) {
        path = QDir(directory).filePath(QStringLiteral("ai-detailed-%1-%2-%3.json")
            .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmsszzz")),
                 QStringLiteral("%1").arg(++m_detailedSequence, 8, 10, QLatin1Char('0')),
                 QUuid::createUuid().toString(QUuid::WithoutBraces)));
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QByteArray bytes = QJsonDocument(detail).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) return false;
    m_detailedPaths.insert(key, path);
    const QFileInfoList files = QDir(directory).entryInfoList(
        {QStringLiteral("ai-detailed-*.json")}, QDir::Files, QDir::Name);
    for (qsizetype index = 0; index < files.size() - 50; ++index) {
        QFile::remove(files.at(index).absoluteFilePath());
    }
    return true;
}

QByteArray AiBattleStatisticsRepository::detailedGamesForCopy(const QString& directory) const {
    const QFileInfoList files = QDir(directory).entryInfoList(
        {QStringLiteral("ai-detailed-*.json")}, QDir::Files, QDir::Name | QDir::Reversed);
    QJsonArray games;
    for (const QFileInfo& info : files) {
        if (games.size() >= 50) break;
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if (document.isObject()) games.append(document.object());
    }
    return games.isEmpty() ? QByteArray{} : QJsonDocument(games).toJson(QJsonDocument::Indented);
}

} // namespace fpdz
