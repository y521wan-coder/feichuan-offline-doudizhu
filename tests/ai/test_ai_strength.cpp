#include <QtTest>

#include <chrono>
#include <iostream>

#include "ai/standard_ai.h"
#include "core/engine/game_engine.h"

using namespace fpdz;

namespace {

struct MatchSummary {
    int challengerLandlordWins = 0;
    int challengerFarmerWins = 0;
    int referenceLandlordWins = 0;
    int referenceFarmerWins = 0;
    int completedGames = 0;
    double maximumChallengerDecisionMs = 0.0;
};

bool isChallengerPlayer(const GameState& state, PlayerId player,
                        bool challengerIsLandlord) {
    const Role role = state.fullState().players[static_cast<int>(player)].role;
    return challengerIsLandlord ? role == Role::Landlord : role == Role::Farmer;
}

MatchSummary runPairedMatches(int playerCount, int firstSeed, int lastSeed) {
    StandardAiPlayer biddingReference(AiDifficulty::Intermediate);
    StandardAiPlayer reference(AiDifficulty::Intermediate);
    StandardAiPlayer challenger(AiDifficulty::Advanced);
    MatchSummary summary;

    for (int seed = firstSeed; seed <= lastSeed; ++seed) {
        // The all-intermediate game supplies the role-specific control for the
        // same deal. The other two games replace exactly one team with Advanced.
        for (int variant = 0; variant < 3; ++variant) {
            const bool challengerIsLandlord = variant == 1;
            const bool challengerIsFarmer = variant == 2;
            GameEngine engine;
            GameCommand start;
            start.type = GameCommandType::StartGame;
            start.randomSeed = static_cast<uint64_t>(seed);
            start.playerCount = playerCount;
            if (!engine.execute(start).success) continue;

            int actionsRemaining = 1000;
            while (actionsRemaining-- > 0 &&
                   engine.state().phase() != GamePhase::Finished) {
                const auto phase = engine.state().phase();
                const PlayerId player = engine.fullState().currentPlayer;
                GameCommand command;
                if (phase == GamePhase::Bidding) {
                    command = biddingReference.decideBid(engine.state(), player);
                } else if (phase == GamePhase::Playing) {
                    const bool useChallenger = challengerIsLandlord
                        ? isChallengerPlayer(engine.state(), player, true)
                        : challengerIsFarmer &&
                            isChallengerPlayer(engine.state(), player, false);
                    AiPlayer& policy = useChallenger
                        ? static_cast<AiPlayer&>(challenger)
                        : static_cast<AiPlayer&>(reference);
                    const auto started = std::chrono::steady_clock::now();
                    command = policy.decidePlay(engine.state(), player);
                    if (useChallenger) {
                        const double elapsed = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - started).count();
                        summary.maximumChallengerDecisionMs = std::max(
                            summary.maximumChallengerDecisionMs, elapsed);
                    }
                } else {
                    break;
                }
                if (!engine.execute(command).success) break;
            }

            if (engine.state().phase() != GamePhase::Finished ||
                !engine.fullState().roundResult.valid) {
                continue;
            }
            ++summary.completedGames;
            const bool landlordWon = engine.fullState().roundResult.landlordWon;
            if (variant == 0) {
                if (landlordWon) ++summary.referenceLandlordWins;
                else ++summary.referenceFarmerWins;
            } else if (challengerIsLandlord && landlordWon) {
                ++summary.challengerLandlordWins;
            } else if (challengerIsFarmer && !landlordWon) {
                ++summary.challengerFarmerWins;
            }
        }
    }
    return summary;
}

} // namespace

class TestAiStrength : public QObject {
    Q_OBJECT

private slots:
    void pairedTwoPlayerStrength() {
        const auto result = runPairedMatches(TWO_PLAYER_COUNT, 1, 100);
        QCOMPARE(result.completedGames, 300);
        std::cout << "TWO_PLAYER_REFERENCE_LANDLORD="
                  << result.referenceLandlordWins
                  << " ADVANCED_LANDLORD=" << result.challengerLandlordWins
                  << " REFERENCE_FARMER=" << result.referenceFarmerWins
                  << " ADVANCED_FARMER=" << result.challengerFarmerWins
                  << " MAX_MS=" << result.maximumChallengerDecisionMs << std::endl;
    }

    void pairedThreePlayerStrength() {
        const auto result = runPairedMatches(THREE_PLAYER_COUNT, 1, 100);
        QCOMPARE(result.completedGames, 300);
        std::cout << "THREE_PLAYER_REFERENCE_LANDLORD="
                  << result.referenceLandlordWins
                  << " ADVANCED_LANDLORD=" << result.challengerLandlordWins
                  << " REFERENCE_FARMER=" << result.referenceFarmerWins
                  << " ADVANCED_FARMER=" << result.challengerFarmerWins
                  << " MAX_MS=" << result.maximumChallengerDecisionMs << std::endl;
    }

    void pairedFourPlayerStrength() {
        const auto result = runPairedMatches(PLAYER_COUNT, 1, 100);
        QCOMPARE(result.completedGames, 300);
        std::cout << "FOUR_PLAYER_REFERENCE_LANDLORD="
                  << result.referenceLandlordWins
                  << " ADVANCED_LANDLORD=" << result.challengerLandlordWins
                  << " REFERENCE_FARMER=" << result.referenceFarmerWins
                  << " ADVANCED_FARMER=" << result.challengerFarmerWins
                  << " MAX_MS=" << result.maximumChallengerDecisionMs << std::endl;
    }
};

QTEST_MAIN(TestAiStrength)
#include "test_ai_strength.moc"
