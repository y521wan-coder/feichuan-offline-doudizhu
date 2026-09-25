#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include "ai/ai_decision_request.h"
#include "ai/standard_ai.h"
#include "app/ai_service_client.h"
#include "core/engine/game_engine.h"

using namespace fpdz;

namespace {

struct Summary {
    int games = 0;
    int completed = 0;
    int aborted = 0;
    int illegal = 0;
    int stale = 0;
    int failures = 0;
    int requests = 0;
    int landlordWins = 0;
    int farmerWins = 0;
    qint64 inputTokens = 0;
    qint64 outputTokens = 0;
    std::vector<qint64> latencies;
    QString firstFailureCode;
    QString firstFailureMessage;
};

void noteFailure(Summary& summary, const AiDecisionResponse& response) {
    ++summary.failures;
    if (summary.firstFailureCode.isEmpty()) {
        summary.firstFailureCode = response.errorCode;
        summary.firstFailureMessage = response.safeMessage;
    }
}

bool waitForDecision(AiServiceClient& client, const QString& requestId,
                     int timeoutMilliseconds, AiDecisionResponse& response) {
    QJsonObject message;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    const auto connection = QObject::connect(
        &client, &AiServiceClient::messageReceived, &loop,
        [&](const QJsonObject& received) {
            if (received.value(QStringLiteral("type")).toString() !=
                QStringLiteral("decision_result")) {
                return;
            }
            if (received.value(QStringLiteral("request_id")).toString() != requestId) {
                return;
            }
            message = received;
            loop.quit();
        });
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMilliseconds);
    loop.exec();
    QObject::disconnect(connection);
    if (message.isEmpty()) return false;
    response = AiDecisionResponse::fromServiceJson(message);
    return true;
}

bool runGame(AiServiceClient& client, int playerCount, uint64_t seed,
             const QString& credentialId, const QString& model,
             CloudStrength strength, int timeoutSeconds, Summary& summary) {
    GameEngine engine;
    GameCommand start;
    start.type = GameCommandType::StartGame;
    start.randomSeed = seed;
    start.playerCount = playerCount;
    if (!engine.execute(start).success) {
        ++summary.illegal;
        return false;
    }
    StandardAiPlayer localHuman(AiDifficulty::Advanced);
    int guard = 4000;
    while (guard-- > 0 && engine.state().phase() != GamePhase::Finished) {
        const GamePhase phase = engine.state().phase();
        const PlayerId player = engine.fullState().currentPlayer;
        const AiObservation observation = makeAiObservation(engine.state(), player);
        if (player == PlayerId::Player1) {
            const GameCommand command = phase == GamePhase::Bidding
                ? localHuman.decideBid(observation)
                : localHuman.decidePlay(observation);
            if (!engine.execute(command).success) {
                ++summary.illegal;
                return false;
            }
            continue;
        }
        SeatControllerConfig controller;
        controller.kind = SeatControllerKind::CloudAi;
        controller.credentialId = credentialId;
        controller.credentialName = QStringLiteral("real");
        controller.model = model;
        controller.timeoutSeconds = timeoutSeconds;
        controller.strength = strength;
        const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const AiDecisionRequest request = AiActionCatalog::create(
            engine.state(), player, controller, requestId);
        ++summary.requests;
        if (!client.requestDecision(request)) {
            ++summary.failures;
            return false;
        }
        AiDecisionResponse response;
        const auto started = std::chrono::steady_clock::now();
        if (!waitForDecision(client, requestId, timeoutSeconds * 1000 + 5000, response)) {
            ++summary.failures;
            client.cancel(requestId);
            return false;
        }
        summary.latencies.push_back(static_cast<qint64>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count()));
        if (response.inputTokens > 0) summary.inputTokens += response.inputTokens;
        if (response.outputTokens > 0) summary.outputTokens += response.outputTokens;
        if (!response.success) {
            noteFailure(summary, response);
            return false;
        }
        if (!response.matches(request, engine.state())) {
            ++summary.stale;
            return false;
        }
        const AiLegalAction* action = AiActionCatalog::find(request, response.actionId);
        if (!action) {
            noteFailure(summary, response);
            return false;
        }
        if (!engine.execute(action->command).success) {
            ++summary.illegal;
            return false;
        }
    }
    if (engine.state().phase() != GamePhase::Finished ||
        !engine.fullState().roundResult.valid) {
        ++summary.aborted;
        return false;
    }
    ++summary.completed;
    if (engine.fullState().roundResult.landlordWon) ++summary.landlordWins;
    else ++summary.farmerWins;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc < 5) {
        std::cerr << "usage: ai_real_model_runner <players 2-4> <games> "
                     "<credential-name> <model> [timeout-seconds]\n";
        return 2;
    }
    const int playerCount = std::stoi(argv[1]);
    const int games = std::stoi(argv[2]);
    const QString credentialName = QString::fromLocal8Bit(argv[3]);
    const QString model = QString::fromLocal8Bit(argv[4]);
    const int timeoutSeconds = argc > 5 ? std::stoi(argv[5]) : 60;
    const QString strengthName = argc > 6 ? QString::fromLocal8Bit(argv[6])
                                          : QStringLiteral("balanced");
    const CloudStrength strength =
        strengthName == QStringLiteral("fast") ? CloudStrength::Fast :
        strengthName == QStringLiteral("deep") ? CloudStrength::Deep :
                                                 CloudStrength::Balanced;
    if (playerCount < TWO_PLAYER_COUNT || playerCount > PLAYER_COUNT || games < 1) {
        std::cerr << "invalid player count or game count\n";
        return 2;
    }

    AiServiceClient client;
    if (!client.start()) {
        std::cerr << "AI service failed to start\n";
        return 1;
    }
    const QJsonObject list = client.requestSync({
        {QStringLiteral("type"), QStringLiteral("credentials_list")}});
    if (!list.value(QStringLiteral("ok")).toBool()) {
        std::cerr << "credential listing failed\n";
        return 1;
    }
    QString credentialId;
    for (const auto& value : list.value(QStringLiteral("credentials")).toArray()) {
        if (value.toObject().value(QStringLiteral("name")).toString() == credentialName) {
            credentialId = value.toObject().value(QStringLiteral("id")).toString();
            break;
        }
    }
    if (credentialId.isEmpty()) {
        std::cerr << "credential not found\n";
        return 1;
    }

    Summary summary;
    summary.games = games;
    for (int offset = 0; offset < games; ++offset) {
        runGame(client, playerCount,
                static_cast<uint64_t>(900000 + offset + playerCount * 1000),
                credentialId, model, strength, timeoutSeconds, summary);
    }
    std::sort(summary.latencies.begin(), summary.latencies.end());
    auto percentile = [&](double fraction) {
        if (summary.latencies.empty()) return qint64{0};
        const std::size_t index = static_cast<std::size_t>(
            fraction * static_cast<double>(summary.latencies.size() - 1));
        return summary.latencies[std::min(index, summary.latencies.size() - 1)];
    };
    std::cout << "REAL_MODEL players=" << playerCount
              << " model=" << model.toStdString()
              << " strength=" << strengthName.toStdString()
              << " games=" << summary.games
              << " completed=" << summary.completed
              << " aborted=" << summary.aborted
              << " illegal=" << summary.illegal
              << " stale=" << summary.stale
              << " failures=" << summary.failures
              << " requests=" << summary.requests
              << " landlord_wins=" << summary.landlordWins
              << " farmer_wins=" << summary.farmerWins
              << " latency_avg_ms="
              << (summary.latencies.empty() ? 0 :
                  std::accumulate(summary.latencies.begin(), summary.latencies.end(),
                                  qint64{0}) / static_cast<qint64>(summary.latencies.size()))
              << " latency_p95_ms=" << percentile(0.95)
              << " input_tokens=" << summary.inputTokens
              << " output_tokens=" << summary.outputTokens
              << " first_failure_code=" << summary.firstFailureCode.toStdString()
              << " first_failure_message=" << summary.firstFailureMessage.toStdString()
              << '\n';
    client.stop();
    return summary.completed == games && summary.illegal == 0 && summary.stale == 0 ? 0 : 1;
}
