#include "ai_battle_types.h"
#include "ai_battle_prompt.h"

#include <QJsonArray>
#include <algorithm>

namespace fpdz {

void SeatControllerConfig::normalize() {
    if (kind != SeatControllerKind::LocalAi && kind != SeatControllerKind::CloudAi) {
        kind = SeatControllerKind::LocalAi;
    }
    const int difficulty = std::clamp(static_cast<int>(localDifficulty), 0, 2);
    localDifficulty = static_cast<AiDifficulty>(difficulty);
    strength = CloudStrength::Fast;
    timeoutSeconds = std::clamp(timeoutSeconds, 1, 180);
    credentialId = credentialId.trimmed();
    credentialName = credentialName.trimmed();
    model = model.trimmed();
}

QJsonObject SeatControllerConfig::toJson() const {
    SeatControllerConfig value = *this;
    value.normalize();
    return {{QStringLiteral("kind"), static_cast<int>(value.kind)},
            {QStringLiteral("localDifficulty"), static_cast<int>(value.localDifficulty)},
            {QStringLiteral("credentialId"), value.credentialId},
            {QStringLiteral("credentialName"), value.credentialName},
            {QStringLiteral("model"), value.model},
            {QStringLiteral("strength"), static_cast<int>(value.strength)},
            {QStringLiteral("timeoutSeconds"), value.timeoutSeconds}};
}

SeatControllerConfig SeatControllerConfig::fromJson(const QJsonObject& json) {
    SeatControllerConfig value;
    value.kind = static_cast<SeatControllerKind>(json.value("kind").toInt());
    value.localDifficulty = static_cast<AiDifficulty>(json.value("localDifficulty").toInt(2));
    value.credentialId = json.value("credentialId").toString();
    value.credentialName = json.value("credentialName").toString();
    value.model = json.value("model").toString();
    value.timeoutSeconds = json.value("timeoutSeconds").toInt(10);
    value.normalize();
    return value;
}

AiBattleSettings::AiBattleSettings() {
    for (int index = 1; index < PLAYER_COUNT; ++index) {
        seats[static_cast<std::size_t>(index)].localDifficulty = AiDifficulty::Advanced;
    }
}

void AiBattleSettings::normalize() {
    if (!isSupportedPlayerCount(playerCount)) playerCount = PLAYER_COUNT;
    autoPassSeconds = std::clamp(autoPassSeconds, 3, 1800);
    seats[0] = SeatControllerConfig{};
    for (auto& seat : seats) seat.normalize();
    strategyPrompt = strategyPrompt.trimmed().left(AI_MAX_STRATEGY_PROMPT_CHARS);
    if (strategyPrompt == defaultAiStrategyPrompt()) strategyPrompt.clear();

    // AI 对战只把玩家一留给真人：其余电脑座位一律使用同一个云模型配置。
    // 以第一个同时填好认证和模型的电脑座位为准，旧配置里的本地机器人和其它模型会被统一覆盖。
    SeatControllerConfig shared;
    for (int index = 1; index < PLAYER_COUNT; ++index) {
        const auto& seat = seats[static_cast<std::size_t>(index)];
        if (!seat.credentialId.isEmpty() && !seat.model.isEmpty()) {
            shared = seat;
            break;
        }
    }
    if (shared.credentialId.isEmpty() && shared.model.isEmpty()) shared = seats[1];
    shared.kind = SeatControllerKind::CloudAi;
    for (int index = 1; index < PLAYER_COUNT; ++index) {
        seats[static_cast<std::size_t>(index)] = shared;
    }
}

bool AiBattleSettings::hasCloudSeat() const {
    for (int index = 1; index < playerCount; ++index) {
        if (seats[static_cast<std::size_t>(index)].kind == SeatControllerKind::CloudAi) {
            return true;
        }
    }
    return false;
}

bool AiBattleSettings::validForStart(QString* reason) const {
    // 玩家二及之后的所有电脑座位共用同一个云模型配置，因此只校验这一个座位。
    const auto& seat = seats[1];
    if (!hasCloudSeat() || seat.credentialId.isEmpty() || seat.model.isEmpty()) {
        if (reason) {
            *reason = QString::fromUtf8(
                u8"AI对战中2及之后的电脑座位都使用云模型，"
                u8"请先选择云认证和模型");
        }
        return false;
    }
    return true;
}

QJsonObject AiBattleSettings::toJson() const {
    AiBattleSettings value = *this;
    value.normalize();
    QJsonArray seatsJson;
    for (const auto& seat : value.seats) seatsJson.append(seat.toJson());
    return {{QStringLiteral("schemaVersion"), 2},
            {QStringLiteral("playerCount"), value.playerCount},
            {QStringLiteral("autoPassEnabled"), value.autoPassEnabled},
            {QStringLiteral("autoPassSeconds"), value.autoPassSeconds},
            {QStringLiteral("landlordMustLeadFirstTurn"), value.landlordMustLeadFirstTurn},
            {QStringLiteral("seats"), seatsJson},
            {QStringLiteral("strategyPrompt"), value.strategyPrompt},
            {QStringLiteral("privacyConsents"), value.privacyConsents}};
}

AiBattleSettings AiBattleSettings::fromJson(const QJsonObject& json) {
    AiBattleSettings value;
    value.playerCount = json.value("playerCount").toInt(PLAYER_COUNT);
    value.autoPassEnabled = json.value("autoPassEnabled").toBool(value.autoPassEnabled);
    value.autoPassSeconds = json.value("autoPassSeconds").toInt(value.autoPassSeconds);
    value.landlordMustLeadFirstTurn = json.value("landlordMustLeadFirstTurn")
        .toBool(value.landlordMustLeadFirstTurn);
    const auto array = json.value("seats").toArray();
    value.privacyConsents = json.value("privacyConsents").toObject();
    value.strategyPrompt = json.value("strategyPrompt").toString();
    for (int index = 0; index < array.size() && index < PLAYER_COUNT; ++index) {
        if (array[index].isObject()) {
            value.seats[static_cast<std::size_t>(index)] =
                SeatControllerConfig::fromJson(array[index].toObject());
            if (json.value("schemaVersion").toInt(1) < 2 &&
                array[index].toObject().value("timeoutSeconds").toInt(15) == 15) {
                value.seats[static_cast<std::size_t>(index)].timeoutSeconds = 10;
            }
        }
    }
    value.normalize();
    return value;
}

QString cloudStrengthName(CloudStrength strength) {
    switch (strength) {
    case CloudStrength::Fast: return QString::fromUtf8(u8"快速");
    case CloudStrength::Deep: return QString::fromUtf8(u8"深入");
    default: return QString::fromUtf8(u8"均衡");
    }
}

QString cloudStrengthApiValue(CloudStrength strength) {
    switch (strength) {
    case CloudStrength::Fast: return QStringLiteral("low");
    case CloudStrength::Deep: return QStringLiteral("high");
    default: return QStringLiteral("medium");
    }
}

} // namespace fpdz
