#include "heuristic_model.h"

#include <QFile>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonParseError>

namespace fpdz {

namespace {

int readInt(const QJsonObject& json, const char* key, int fallback) {
    const auto value = json.value(QLatin1String(key));
    return value.isDouble() ? value.toInt(fallback) : fallback;
}

bool inRange(int value, int minimum, int maximum) {
    return value >= minimum && value <= maximum;
}

void setError(QString* error, const QString& message) {
    if (error) *error = message;
}

} // namespace

HeuristicWeights HeuristicWeights::defaults() {
    return HeuristicWeights{};
}

bool HeuristicWeights::isValid() const {
    return inRange(freeMoveCardReward, 20, 500) &&
           inRange(responseMoveCardReward, 20, 500) &&
           inRange(leaderRankPenalty, 0, 50) &&
           inRange(responseRankPenalty, 0, 80) &&
           inRange(remainingCardPenalty, 0, 100) &&
           inRange(remainingGroupPenalty, 0, 300) &&
           inRange(finishBonus, 10000, 500000) &&
           inRange(landlordDangerBonus, 0, 5000) &&
           inRange(landlordResponseBonus, 0, 3000) &&
           inRange(farmerOvertakePenalty, 0, 20000) &&
           inRange(urgentBombPenalty, 0, 5000) &&
           inRange(normalBombPenalty, 0, 10000) &&
           inRange(nearFinishBonus, 0, 5000) &&
           inRange(followUpPenalty, 0, 100) &&
           inRange(largestFollowUpBonus, 0, 500) &&
           inRange(bombReserveDangerBonus, 0, 5000) &&
           inRange(bombReserveBonus, 0, 3000) &&
           inRange(dangerMoveCardBonus, 0, 1000) &&
           inRange(endgameLargestBonus, 0, 1000) &&
           inRange(endgameShapeMultiplier, 0, 10) &&
           inRange(bidBombWeight, 0, 20) &&
           inRange(bidBigJokerWeight, 0, 20) &&
           inRange(bidSmallJokerWeight, 0, 20) &&
           inRange(bidTwoWeight, 0, 10) &&
           inRange(bidThresholdOne, 1, 30) &&
           inRange(bidThresholdTwo, bidThresholdOne + 1, 40) &&
           inRange(bidThresholdThree, bidThresholdTwo + 1, 50) &&
           inRange(bidPassAdjustment, -20, 20);
}

QJsonObject HeuristicWeights::toJson() const {
    QJsonObject json;
    json["freeMoveCardReward"] = freeMoveCardReward;
    json["responseMoveCardReward"] = responseMoveCardReward;
    json["leaderRankPenalty"] = leaderRankPenalty;
    json["responseRankPenalty"] = responseRankPenalty;
    json["remainingCardPenalty"] = remainingCardPenalty;
    json["remainingGroupPenalty"] = remainingGroupPenalty;
    json["finishBonus"] = finishBonus;
    json["landlordDangerBonus"] = landlordDangerBonus;
    json["landlordResponseBonus"] = landlordResponseBonus;
    json["farmerOvertakePenalty"] = farmerOvertakePenalty;
    json["urgentBombPenalty"] = urgentBombPenalty;
    json["normalBombPenalty"] = normalBombPenalty;
    json["nearFinishBonus"] = nearFinishBonus;
    json["followUpPenalty"] = followUpPenalty;
    json["largestFollowUpBonus"] = largestFollowUpBonus;
    json["bombReserveDangerBonus"] = bombReserveDangerBonus;
    json["bombReserveBonus"] = bombReserveBonus;
    json["dangerMoveCardBonus"] = dangerMoveCardBonus;
    json["endgameLargestBonus"] = endgameLargestBonus;
    json["endgameShapeMultiplier"] = endgameShapeMultiplier;
    json["bidBombWeight"] = bidBombWeight;
    json["bidBigJokerWeight"] = bidBigJokerWeight;
    json["bidSmallJokerWeight"] = bidSmallJokerWeight;
    json["bidTwoWeight"] = bidTwoWeight;
    json["bidThresholdOne"] = bidThresholdOne;
    json["bidThresholdTwo"] = bidThresholdTwo;
    json["bidThresholdThree"] = bidThresholdThree;
    json["bidPassAdjustment"] = bidPassAdjustment;
    return json;
}

std::optional<HeuristicWeights> HeuristicWeights::fromJson(const QJsonObject& json,
                                                            QString* error) {
    HeuristicWeights value;
    value.freeMoveCardReward = readInt(json, "freeMoveCardReward", value.freeMoveCardReward);
    value.responseMoveCardReward = readInt(json, "responseMoveCardReward", value.responseMoveCardReward);
    value.leaderRankPenalty = readInt(json, "leaderRankPenalty", value.leaderRankPenalty);
    value.responseRankPenalty = readInt(json, "responseRankPenalty", value.responseRankPenalty);
    value.remainingCardPenalty = readInt(json, "remainingCardPenalty", value.remainingCardPenalty);
    value.remainingGroupPenalty = readInt(json, "remainingGroupPenalty", value.remainingGroupPenalty);
    value.finishBonus = readInt(json, "finishBonus", value.finishBonus);
    value.landlordDangerBonus = readInt(json, "landlordDangerBonus", value.landlordDangerBonus);
    value.landlordResponseBonus = readInt(json, "landlordResponseBonus", value.landlordResponseBonus);
    value.farmerOvertakePenalty = readInt(json, "farmerOvertakePenalty", value.farmerOvertakePenalty);
    value.urgentBombPenalty = readInt(json, "urgentBombPenalty", value.urgentBombPenalty);
    value.normalBombPenalty = readInt(json, "normalBombPenalty", value.normalBombPenalty);
    value.nearFinishBonus = readInt(json, "nearFinishBonus", value.nearFinishBonus);
    value.followUpPenalty = readInt(json, "followUpPenalty", value.followUpPenalty);
    value.largestFollowUpBonus = readInt(json, "largestFollowUpBonus", value.largestFollowUpBonus);
    value.bombReserveDangerBonus = readInt(json, "bombReserveDangerBonus", value.bombReserveDangerBonus);
    value.bombReserveBonus = readInt(json, "bombReserveBonus", value.bombReserveBonus);
    value.dangerMoveCardBonus = readInt(json, "dangerMoveCardBonus", value.dangerMoveCardBonus);
    value.endgameLargestBonus = readInt(json, "endgameLargestBonus", value.endgameLargestBonus);
    value.endgameShapeMultiplier = readInt(json, "endgameShapeMultiplier", value.endgameShapeMultiplier);
    value.bidBombWeight = readInt(json, "bidBombWeight", value.bidBombWeight);
    value.bidBigJokerWeight = readInt(json, "bidBigJokerWeight", value.bidBigJokerWeight);
    value.bidSmallJokerWeight = readInt(json, "bidSmallJokerWeight", value.bidSmallJokerWeight);
    value.bidTwoWeight = readInt(json, "bidTwoWeight", value.bidTwoWeight);
    value.bidThresholdOne = readInt(json, "bidThresholdOne", value.bidThresholdOne);
    value.bidThresholdTwo = readInt(json, "bidThresholdTwo", value.bidThresholdTwo);
    value.bidThresholdThree = readInt(json, "bidThresholdThree", value.bidThresholdThree);
    value.bidPassAdjustment = readInt(json, "bidPassAdjustment", value.bidPassAdjustment);
    if (!value.isValid()) {
        setError(error, QStringLiteral("模型参数超出安全范围"));
        return std::nullopt;
    }
    return value;
}

QJsonObject HeuristicModelPackage::toJson() const {
    QJsonObject json;
    json["schemaVersion"] = 2;
    json["modelKind"] = modelKind.isEmpty() ? QStringLiteral("heuristic_search_v2") : modelKind;
    json["modelId"] = modelId;
    json["createdAtUtc"] = createdAtUtc;
    json["ruleProfile"] = QStringLiteral("FourPlayerStandardV1");
    json["ruleFingerprint"] = ruleFingerprint;
    if (!tierLabel.isEmpty()) json["tier"] = tierLabel;
    json["promotionEligible"] = promotionEligible;
    json["trainingGames"] = trainingGames;
    QJsonObject evaluation;
    evaluation["scoreDelta"] = evaluationScoreDelta;
    evaluation["confidenceLowerBound"] = evaluationConfidenceLowerBound;
    evaluation["landlordWinRate"] = landlordWinRate;
    evaluation["farmerWinRate"] = farmerWinRate;
    evaluation["landlordConfidenceLowerBound"] = landlordConfidenceLowerBound;
    evaluation["farmerConfidenceLowerBound"] = farmerConfidenceLowerBound;
    evaluation["cascadeScoreDelta"] = cascadeScoreDelta;
    evaluation["cascadeConfidenceLowerBound"] = cascadeConfidenceLowerBound;
    evaluation["cascadeLandlordConfidenceLowerBound"] =
        cascadeLandlordConfidenceLowerBound;
    evaluation["cascadeFarmerConfidenceLowerBound"] =
        cascadeFarmerConfidenceLowerBound;
    evaluation["protocol"] = evaluationProtocol;
    evaluation["seedGroups"] = evaluationSeedGroups;
    evaluation["seedManifestSha256"] = evaluationSeedManifestSha256;
    json["evaluation"] = evaluation;
    json["weights"] = weights.toJson();
    const QByteArray canonical = QJsonDocument(json).toJson(QJsonDocument::Compact);
    json["contentSha256"] = QString::fromLatin1(
        QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex());
    return json;
}

std::optional<HeuristicModelPackage> HeuristicModelPackage::fromJson(
    const QJsonObject& json, QString* error) {
    const int schemaVersion = json.value("schemaVersion").toInt();
    const QString modelKind = json.value("modelKind").toString();
    const bool legacy = schemaVersion == 1 && modelKind == QStringLiteral("heuristic_v1");
    const bool current = schemaVersion == 2 &&
        (modelKind == QStringLiteral("heuristic_search_v2") ||
         modelKind == QStringLiteral("heuristic_v1"));
    if (!legacy && !current) {
        setError(error, QStringLiteral("不支持的模型格式"));
        return std::nullopt;
    }
    const QString storedHash = json.value("contentSha256").toString().toLower();
    QJsonObject canonicalJson = json;
    canonicalJson.remove("contentSha256");
    const QString calculatedHash = QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(canonicalJson).toJson(QJsonDocument::Compact),
                                 QCryptographicHash::Sha256).toHex());
    if (storedHash.size() != 64 || storedHash != calculatedHash) {
        setError(error, QStringLiteral("模型完整性校验失败"));
        return std::nullopt;
    }
    HeuristicModelPackage package;
    package.schemaVersion = schemaVersion;
    package.modelKind = modelKind;
    package.modelId = json.value("modelId").toString();
    package.createdAtUtc = json.value("createdAtUtc").toString();
    package.ruleFingerprint = json.value("ruleFingerprint").toString();
    package.tierLabel = json.value("tier").toString();
    package.promotionEligible = json.value("promotionEligible").toBool(false);
    package.trainingGames = json.value("trainingGames").toVariant().toLongLong();
    const auto evaluation = json.value("evaluation").toObject();
    package.evaluationScoreDelta = evaluation.value("scoreDelta").toDouble();
    package.evaluationConfidenceLowerBound =
        evaluation.value("confidenceLowerBound").toDouble();
    package.landlordWinRate = evaluation.value("landlordWinRate").toDouble();
    package.farmerWinRate = evaluation.value("farmerWinRate").toDouble();
    package.landlordConfidenceLowerBound =
        evaluation.value("landlordConfidenceLowerBound").toDouble();
    package.farmerConfidenceLowerBound =
        evaluation.value("farmerConfidenceLowerBound").toDouble();
    package.cascadeScoreDelta = evaluation.value("cascadeScoreDelta").toDouble();
    package.cascadeConfidenceLowerBound =
        evaluation.value("cascadeConfidenceLowerBound").toDouble();
    package.cascadeLandlordConfidenceLowerBound =
        evaluation.value("cascadeLandlordConfidenceLowerBound").toDouble();
    package.cascadeFarmerConfidenceLowerBound =
        evaluation.value("cascadeFarmerConfidenceLowerBound").toDouble();
    package.evaluationProtocol = evaluation.value("protocol").toString(
        legacy ? QStringLiteral("legacy_paired_v1") : QStringLiteral("paired_roles_v2"));
    package.evaluationSeedGroups = evaluation.value("seedGroups").toInt();
    package.evaluationSeedManifestSha256 =
        evaluation.value("seedManifestSha256").toString();
    const auto weights = HeuristicWeights::fromJson(json.value("weights").toObject(), error);
    if (!weights) return std::nullopt;
    package.weights = *weights;
    if (package.modelId.isEmpty() ||
        package.ruleFingerprint != QLatin1String(FOUR_PLAYER_STANDARD_V1_FINGERPRINT)) {
        setError(error, QStringLiteral("模型与四人斗地主标准规则不兼容"));
        return std::nullopt;
    }
    return package;
}

std::optional<HeuristicModelPackage> HeuristicModelPackage::loadFile(
    const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("无法打开模型文件"));
        return std::nullopt;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QStringLiteral("模型文件不是有效JSON"));
        return std::nullopt;
    }
    return fromJson(document.object(), error);
}

} // namespace fpdz
