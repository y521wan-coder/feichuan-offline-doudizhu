#pragma once

#include <QJsonObject>
#include <QString>
#include <optional>

namespace fpdz {

inline constexpr const char* FOUR_PLAYER_STANDARD_V1_FINGERPRINT =
    "e56e82c6e198532653890f194389871c7ac746b65d3776bd8ae2bd5cf83cc944";

struct HeuristicWeights {
    int freeMoveCardReward = 140;
    int responseMoveCardReward = 95;
    int leaderRankPenalty = 2;
    int responseRankPenalty = 8;
    int remainingCardPenalty = 8;
    int remainingGroupPenalty = 45;
    int finishBonus = 100000;
    int landlordDangerBonus = 500;
    int landlordResponseBonus = 160;
    int farmerOvertakePenalty = 2500;
    int urgentBombPenalty = 120;
    int normalBombPenalty = 700;
    int nearFinishBonus = 450;
    int followUpPenalty = 2;
    int largestFollowUpBonus = 35;
    int bombReserveDangerBonus = 180;
    int bombReserveBonus = 90;
    int dangerMoveCardBonus = 95;
    int endgameLargestBonus = 70;
    int endgameShapeMultiplier = 1;
    int bidBombWeight = 3;
    int bidBigJokerWeight = 2;
    int bidSmallJokerWeight = 1;
    int bidTwoWeight = 1;
    int bidThresholdOne = 4;
    int bidThresholdTwo = 7;
    int bidThresholdThree = 10;
    int bidPassAdjustment = 0;

    static HeuristicWeights defaults();
    bool isValid() const;
    QJsonObject toJson() const;
    static std::optional<HeuristicWeights> fromJson(const QJsonObject& json,
                                                     QString* error = nullptr);
};

struct HeuristicModelPackage {
    int schemaVersion = 2;
    QString modelKind = QStringLiteral("heuristic_search_v2");
    QString modelId;
    QString createdAtUtc;
    QString ruleFingerprint;
    QString tierLabel;
    QString evaluationProtocol = QStringLiteral("paired_roles_v2");
    bool promotionEligible = false;
    qint64 trainingGames = 0;
    double evaluationScoreDelta = 0.0;
    double evaluationConfidenceLowerBound = 0.0;
    double landlordWinRate = 0.0;
    double farmerWinRate = 0.0;
    double landlordConfidenceLowerBound = 0.0;
    double farmerConfidenceLowerBound = 0.0;
    double cascadeScoreDelta = 0.0;
    double cascadeConfidenceLowerBound = 0.0;
    double cascadeLandlordConfidenceLowerBound = 0.0;
    double cascadeFarmerConfidenceLowerBound = 0.0;
    int evaluationSeedGroups = 0;
    QString evaluationSeedManifestSha256;
    HeuristicWeights weights;

    QJsonObject toJson() const;
    static std::optional<HeuristicModelPackage> fromJson(const QJsonObject& json,
                                                          QString* error = nullptr);
    static std::optional<HeuristicModelPackage> loadFile(const QString& path,
                                                          QString* error = nullptr);
};

} // namespace fpdz
