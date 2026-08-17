#include <QtTest>
#include <QUrlQuery>
#include "update_service.h"

using namespace fpdz;

class TestUpdateService : public QObject {
    Q_OBJECT
private slots:
    void testBuildCheckUrlContainsFixedParameters() {
        const QUrl url = UpdateService::buildCheckUrl(QStringLiteral("1.0"));
        QCOMPARE(url.scheme(), QStringLiteral("https"));
        QCOMPARE(url.host(), QStringLiteral("update.327802521.xyz"));
        const QUrlQuery query(url);
        QCOMPARE(query.queryItemValue(QStringLiteral("product_key")),
                 QStringLiteral("feichuan_offline_doudizhu"));
        QCOMPARE(query.queryItemValue(QStringLiteral("platform")), QStringLiteral("windows"));
        QCOMPARE(query.queryItemValue(QStringLiteral("channel")), QStringLiteral("stable"));
        QCOMPARE(query.queryItemValue(QStringLiteral("current_version")), QStringLiteral("1.0"));
    }

    void testParseAvailableUpdate() {
        const QByteArray payload = R"({
            "code": 0,
            "message": "ok",
            "data": {
                "update_available": true,
                "latest_version": "1.1",
                "release_notes": "更新说明",
                "download_url": "https://update.327802521.xyz/api/v1/updates/download/8/",
                "file_size": 12345,
                "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
            }
        })";
        const auto result = UpdateService::parseCheckResponse(payload);
        QVERIFY(result.success);
        QVERIFY(result.updateAvailable);
        QCOMPARE(result.latestVersion, QStringLiteral("1.1"));
        QCOMPARE(result.fileSize, 12345);
        QCOMPARE(result.downloadUrl.scheme(), QStringLiteral("https"));
        QCOMPARE(result.sha256.size(), 64);
    }

    void testParseNoUpdate() {
        const auto result = UpdateService::parseCheckResponse(
            R"({"code":0,"message":"ok","data":{"update_available":false}})");
        QVERIFY(result.success);
        QVERIFY(!result.updateAvailable);
    }

    void testRejectsUnsafeOrIncompleteUpdate() {
        const auto result = UpdateService::parseCheckResponse(
            R"({"code":0,"message":"ok","data":{"update_available":true,"latest_version":"1.1","download_url":"http://example.com/file.exe","sha256":"bad"}})");
        QVERIFY(!result.success);
        QVERIFY(!result.errorMessage.isEmpty());
    }
};

QTEST_MAIN(TestUpdateService)
#include "test_update_service.moc"
