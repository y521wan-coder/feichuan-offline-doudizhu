#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSet>
#include <QTemporaryDir>

#include "persistence/ai_battle_statistics_repository.h"

using namespace fpdz;

class TestAiBattlePersistence : public QObject {
    Q_OBJECT
private slots:
    void testStatisticsAndReplaySummaryStaySanitized() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AiBattleStatisticsRepository repository;
        const QString statisticsPath = directory.filePath(QStringLiteral("statistics.json"));
        QVERIFY(repository.load(statisticsPath));

        SeatControllerConfig cloud;
        cloud.kind = SeatControllerKind::CloudAi;
        cloud.credentialId = QStringLiteral("SENSITIVE-CREDENTIAL-ID");
        cloud.credentialName = QStringLiteral("SENSITIVE-CREDENTIAL-NAME");
        cloud.model = QStringLiteral("safe-model-name");
        repository.recordRequest(1, cloud, false, QStringLiteral("timeout"),
                                 3000, 17, 2);

        FullGameState state;
        state.gameId = 42;
        state.activePlayerCount = THREE_PLAYER_COUNT;
        state.players[0].role = Role::Landlord;
        PublicActionRecord play;
        play.type = PublicActionType::Play;
        play.playerId = PlayerId::Player2;
        play.sequence = 7;
        play.cards.push_back(Card::create(0));
        state.actionHistory.push_back(play);
        RoundResult result;
        result.valid = true;
        result.winner = PlayerId::Player1;
        result.landlordWon = true;
        result.finalMultiplier = 4;
        result.scoreChanges = {6, -3, -3, 0};
        state.roundResult = result;
        AiBattleSettings settings;
        settings.playerCount = THREE_PLAYER_COUNT;
        settings.seats[1] = cloud;
        settings.seats[2].kind = SeatControllerKind::LocalAi;
        settings.normalize();

        AiDecisionRequest request;
        request.requestId = QStringLiteral("safe-request-id");
        request.gameId = state.gameId;
        request.eventSequence = 7;
        request.phase = GamePhase::Playing;
        request.playerId = PlayerId::Player2;
        request.controller = cloud;
        request.strategyPrompt = QString::fromUtf8(u8"测试自定义农民协作策略");
        request.observation = {
            {QStringLiteral("player_count"), THREE_PLAYER_COUNT},
            {QStringLiteral("highest_bid"), 3},
            {QStringLiteral("base_score"), 3},
            {QStringLiteral("multiplier"), 2},
            {QStringLiteral("landlord"), 0},
            {QStringLiteral("remaining_cards"), QJsonArray{8, 7, 6}},
            {QStringLiteral("last_played_by"), 0},
            {QStringLiteral("consecutive_passes"), 0},
            {QStringLiteral("history"), QJsonArray{QJsonObject{
                {QStringLiteral("cards"), QStringLiteral("SENSITIVE-HIDDEN-CARDS")}}}},
            {QStringLiteral("all_hands"), QStringLiteral("SENSITIVE-ALL-HANDS")},
            {QStringLiteral("bottom_cards"), QStringLiteral("SENSITIVE-BOTTOM-CARDS")}};
        AiLegalAction action;
        action.actionId = 9;
        action.description = {
            {QStringLiteral("rank_signature"), QStringLiteral("SENSITIVE-ACTION-CARDS")}};
        request.actions.append(action);

        repository.recordRound(result, state, settings);
        QVERIFY(repository.save(statisticsPath));
        const QString logsDirectory = directory.filePath(QStringLiteral("logs"));
        QVERIFY(repository.recordGameStarted(logsDirectory, state, settings));
        repository.recordPublicAction(state);
        QVERIFY(repository.recordDecisionStarted(logsDirectory, request, 1234));
        const QString detailedDirectory = directory.filePath(QStringLiteral("replays/detailed"));
        QVERIFY(repository.saveDetailedGame(detailedDirectory, result, state, settings));
        QVERIFY(repository.recordDecisionFinished(
            logsDirectory, request, false, QStringLiteral("service_error"),
            QStringLiteral("timeout"), 3000, 17, 2, 9));
        QVERIFY(repository.recordRoundFinished(logsDirectory, result, state, settings));
        QVERIFY(repository.saveReplaySummary(directory.filePath(QStringLiteral("replays")),
                                             result, state, settings));
        QVERIFY(repository.saveDetailedGame(detailedDirectory, result, state, settings));
        const QByteArray copied = repository.detailedGamesForCopy(detailedDirectory);
        QCOMPARE(QDir(detailedDirectory).entryList({QStringLiteral("ai-detailed-*.json")},
                                                   QDir::Files).size(), 1);
        const QJsonObject detail = QJsonDocument::fromJson(copied).array().first().toObject();
        QCOMPARE(detail.value("actions").toArray().first().toObject()
                     .value("cards").toArray().first().toObject().value("id").toInt(), 0);
        QVERIFY(detail.value("actions").toArray().first().toObject()
                    .contains("remainingCardCountsAfter"));
        QCOMPARE(detail.value("cloudDecisions").toArray().first().toObject()
                     .value("latencySeconds").toDouble(), 3.0);
        QCOMPARE(detail.value("cloudDecisions").toArray().first().toObject()
                     .value("outcome").toString(), QStringLiteral("service_error"));
        QCOMPARE(detail.value("cloudDecisions").toArray().first().toObject()
                     .value("strategyPrompt").toString(), request.strategyPrompt);
        QVERIFY(copied.contains("cloudDecisions"));
        QVERIFY(copied.contains("latencySeconds"));
        QVERIFY(copied.contains("3000"));
        QVERIFY(copied.contains("cards"));
        QVERIFY(copied.contains("timeout"));
        QVERIFY(!copied.contains("SENSITIVE-CREDENTIAL"));
        QVERIFY(!copied.contains("SENSITIVE-HIDDEN-CARDS"));
        QVERIFY(!copied.contains("SENSITIVE-ALL-HANDS"));
        QVERIFY(!copied.contains("SENSITIVE-BOTTOM-CARDS"));
        QVERIFY(!copied.contains("SENSITIVE-ACTION-CARDS"));

        QFile statistics(statisticsPath);
        QVERIFY(statistics.open(QIODevice::ReadOnly));
        const QByteArray statisticsBytes = statistics.readAll();
        QVERIFY(!statisticsBytes.contains("SENSITIVE-CREDENTIAL"));
        QVERIFY(statisticsBytes.contains("safe-model-name"));
        QCOMPARE(repository.data().value("failureTypes").toObject()
                     .value("timeout").toInt(), 1);
        QCOMPARE(repository.data().value("rounds").toArray().first().toObject()
                     .value("landlord").toInt(), 0);

        const QStringList replayFiles = QDir(directory.filePath(QStringLiteral("replays")))
            .entryList({QStringLiteral("*.json")}, QDir::Files);
        QCOMPARE(replayFiles.size(), 1);
        QFile replay(QDir(directory.filePath(QStringLiteral("replays")))
                         .filePath(replayFiles.first()));
        QVERIFY(replay.open(QIODevice::ReadOnly));
        const QByteArray replayBytes = replay.readAll();
        QVERIFY(!replayBytes.contains("SENSITIVE-CREDENTIAL"));
        QVERIFY(!replayBytes.contains("bottomCards"));
        QVERIFY(!replayBytes.contains("rank"));
        QVERIFY(!replayBytes.contains("cardIds"));
        QVERIFY(replayBytes.contains("cardCount"));

        const QStringList traceFiles = QDir(logsDirectory)
            .entryList({QStringLiteral("ai-game-*.jsonl")}, QDir::Files);
        QCOMPARE(traceFiles.size(), 1);
        QFile trace(QDir(logsDirectory).filePath(traceFiles.first()));
        QVERIFY(trace.open(QIODevice::ReadOnly));
        const QByteArray traceBytes = trace.readAll();
        QCOMPARE(traceBytes.count('\n'), 4);
        QVERIFY(traceBytes.contains("decision_started"));
        QVERIFY(traceBytes.contains("decision_finished"));
        QVERIFY(traceBytes.contains("timeout"));
        QVERIFY(traceBytes.contains("safe-model-name"));
        QVERIFY(!traceBytes.contains("SENSITIVE-CREDENTIAL"));
        QVERIFY(!traceBytes.contains("SENSITIVE-HIDDEN-CARDS"));
        QVERIFY(!traceBytes.contains("SENSITIVE-ALL-HANDS"));
        QVERIFY(!traceBytes.contains("SENSITIVE-BOTTOM-CARDS"));
        QVERIFY(!traceBytes.contains("SENSITIVE-ACTION-CARDS"));
    }

    void testDetailedGamesKeepLatestFiftyCompletedGames() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AiBattleStatisticsRepository repository;
        const QString path = directory.filePath(QStringLiteral("detailed"));
        FullGameState state;
        state.activePlayerCount = TWO_PLAYER_COUNT;
        state.players[0].role = Role::Landlord;
        RoundResult result;
        result.valid = true;
        AiBattleSettings settings;
        settings.playerCount = TWO_PLAYER_COUNT;
        for (int game = 1; game <= 51; ++game) {
            state.gameId = game;
            QVERIFY(repository.saveDetailedGame(path, result, state, settings));
        }
        const auto files = QDir(path).entryList({QStringLiteral("ai-detailed-*.json")}, QDir::Files);
        QCOMPARE(files.size(), 50);
        const QJsonArray games = QJsonDocument::fromJson(
            repository.detailedGamesForCopy(path)).array();
        QCOMPARE(games.size(), 50);
        QSet<int> gameIds;
        for (const QJsonValue& game : games) gameIds.insert(game.toObject().value("gameId").toInt());
        QVERIFY(!gameIds.contains(1));
        QVERIFY(gameIds.contains(2));
        QVERIFY(gameIds.contains(51));
    }
};

QTEST_MAIN(TestAiBattlePersistence)
#include "test_ai_battle_persistence.moc"
