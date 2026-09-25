#pragma once

#include "../ai/ai_difficulty.h"
#include "../core/model/player.h"

#include <QJsonObject>
#include <QString>
#include <array>

namespace fpdz {

enum class SeatControllerKind { LocalAi = 0, CloudAi = 1 };
enum class CloudStrength { Fast = 0, Balanced = 1, Deep = 2 };
enum class ApiProtocol { OpenAiResponses = 0, CompatibleResponses = 1,
                         CompatibleChatCompletions = 2 };
enum class ApiAuthMethod { Bearer = 0, ApiKey = 1, XApiKey = 2,
                           CustomHeader = 3, None = 4 };

struct SeatControllerConfig {
    SeatControllerKind kind = SeatControllerKind::LocalAi;
    AiDifficulty localDifficulty = AiDifficulty::Advanced;
    QString credentialId;
    QString credentialName;
    QString model;
    CloudStrength strength = CloudStrength::Fast;
    int timeoutSeconds = 10;

    void normalize();
    QJsonObject toJson() const;
    static SeatControllerConfig fromJson(const QJsonObject& json);
};

struct AiBattleSettings {
    int playerCount = PLAYER_COUNT;
    bool autoPassEnabled = true;
    int autoPassSeconds = 30;
    bool landlordMustLeadFirstTurn = true;
    std::array<SeatControllerConfig, PLAYER_COUNT> seats{};
    QJsonObject privacyConsents;
    // Empty means the built-in strategy text. Shared by all cloud seats.
    QString strategyPrompt;

    AiBattleSettings();
    void normalize();
    bool hasCloudSeat() const;
    bool validForStart(QString* reason = nullptr) const;
    QJsonObject toJson() const;
    static AiBattleSettings fromJson(const QJsonObject& json);
};

QString cloudStrengthName(CloudStrength strength);
QString cloudStrengthApiValue(CloudStrength strength);

} // namespace fpdz
