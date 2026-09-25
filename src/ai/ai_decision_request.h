#pragma once

#include "ai_player.h"
#include "../app/ai_battle_types.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace fpdz {

constexpr int AI_SERVICE_PROTOCOL_VERSION = 1;
constexpr qsizetype AI_MAX_MESSAGE_BYTES = 128 * 1024;
constexpr int AI_MAX_ACTIONS = 512;

struct AiLegalAction {
    int actionId = -1;
    GameCommand command;
    QJsonObject description;
    QString semanticSignature;
};

struct AiDecisionRequest {
    QString requestId;
    uint64_t gameId = 0;
    uint64_t eventSequence = 0;
    GamePhase phase = GamePhase::NotStarted;
    PlayerId playerId = PlayerId::Player1;
    SeatControllerConfig controller;
    QString strategyPrompt;
    QJsonObject observation;
    QVector<AiLegalAction> actions;

    QJsonObject toServiceJson() const;
};

struct AiDecisionResponse {
    QString requestId;
    uint64_t gameId = 0;
    uint64_t eventSequence = 0;
    GamePhase phase = GamePhase::NotStarted;
    PlayerId playerId = PlayerId::Player1;
    bool success = false;
    int actionId = -1;
    QString errorCode;
    QString safeMessage;
    int latencyMilliseconds = 0;
    qint64 inputTokens = -1;
    qint64 outputTokens = -1;

    static AiDecisionResponse fromServiceJson(const QJsonObject& json);
    bool matches(const AiDecisionRequest& request, const GameState& currentState) const;
};

class AiActionCatalog {
public:
    static AiDecisionRequest create(const GameState& state, PlayerId playerId,
                                    const SeatControllerConfig& controller,
                                    const QString& requestId);
    static const AiLegalAction* find(const AiDecisionRequest& request, int actionId);
};

} // namespace fpdz
