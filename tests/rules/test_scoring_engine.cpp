#include <QtTest>
#include "core/rules/scoring_engine.h"
using namespace fpdz;
class TestScoringEngine : public QObject {
    Q_OBJECT
private slots:
    void testMultiplierCap() {
        QCOMPARE(ScoringEngine::applyMultiplierCap(8192), static_cast<int64_t>(4096));
        QCOMPARE(ScoringEngine::applyMultiplierCap(100), static_cast<int64_t>(100));
    }
    void testZeroSum() {
        ScoreResult r;
        r.scoreChanges = {100, -33, -33, -34};
        QVERIFY(r.isZeroSum());
    }
};
QTEST_MAIN(TestScoringEngine)
#include "test_scoring_engine.moc"
