#include <QtTest>

#include "ai/standard_ai.h"
#include "core/engine/game_engine.h"

using namespace fpdz;

class TestEngineSimulation : public QObject {
    Q_OBJECT

private:
    void runIntermediateGames(int firstSeed, int lastSeed) {
        StandardAiPlayer ai(AiDifficulty::Intermediate);
        int gamesCompleted = 0;

        for (int seed = firstSeed; seed <= lastSeed; ++seed) {
            GameEngine engine;
            GameCommand startCommand;
            startCommand.type = GameCommandType::StartGame;
            startCommand.randomSeed = seed;
            QVERIFY(engine.execute(startCommand).success);

            int remainingTurns = 1000;
            while (remainingTurns-- > 0) {
                const auto phase = engine.state().phase();
                if (phase == GamePhase::Finished) {
                    const auto& round = engine.state().fullState().roundResult;
                    QVERIFY(round.valid);
                    int64_t scoreTotal = 0;
                    for (const auto score : round.scoreChanges) scoreTotal += score;
                    QCOMPARE(scoreTotal, int64_t(0));
                    gamesCompleted++;
                    break;
                }

                const auto currentPlayer = engine.state().fullState().currentPlayer;
                GameCommand command;
                if (phase == GamePhase::Bidding) {
                    command = ai.decideBid(engine.state(), currentPlayer);
                } else if (phase == GamePhase::Playing) {
                    command = ai.decidePlay(engine.state(), currentPlayer);
                } else {
                    QFAIL("Game entered an unexpected non-terminal phase.");
                }

                const auto result = engine.execute(command);
                QVERIFY2(result.success, "AI generated an illegal command.");
            }
        }

        QCOMPARE(gamesCompleted, lastSeed - firstSeed + 1);
    }

private slots:
    void testSeeds1To250() {
        runIntermediateGames(1, 250);
    }

    void testSeeds251To500() {
        runIntermediateGames(251, 500);
    }
};

QTEST_MAIN(TestEngineSimulation)
#include "test_engine_simulation.moc"
