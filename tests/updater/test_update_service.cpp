#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>

#include "signature_verifier.h"
#include "update_service.h"

using namespace fpdz;

namespace {
QByteArray fixedSha256() {
    return QByteArray("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
}
ReleaseSignatureData fixedSignatureData() {
    ReleaseSignatureData data;
    data.productKey = QStringLiteral("feichuan_offline_doudizhu");
    data.platform = QStringLiteral("windows");
    data.channel = QStringLiteral("stable");
    data.version = QStringLiteral("1.0");
    data.fileSize = 123456;
    data.sha256 = fixedSha256();
    data.algorithm = QStringLiteral("rsa-pkcs1-sha256");
    data.payloadVersion = 1;
    return data;
}
QByteArray readSourceFile(const QString& relativePath) {
    QFile file(QStringLiteral(FPDZ_SOURCE_DIR) + QLatin1Char('/') + relativePath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
} // namespace

class TestUpdateService : public QObject {
    Q_OBJECT
private slots:
    void testBuildCheckUrlContainsFixedParameters();
    void testParseAvailableUpdateRequiresSignedFields();
    void testParseNoUpdate();
    void testRejectsUnsafeOrIncompleteUpdate();
    void testCanonicalSignaturePayload();
    void testProductionPublicKeyMatchesTrackedKeyId();
    void testPinnedReleaseSignatureAndTamperRejection();
};

void TestUpdateService::testBuildCheckUrlContainsFixedParameters() {
    const QUrl url = UpdateService::buildCheckUrl(QStringLiteral("1.0"));
    QCOMPARE(url.scheme(), QStringLiteral("https"));
    QCOMPARE(url.host(), QStringLiteral("update.327802521.xyz"));
    const QUrlQuery query(url);
    QCOMPARE(query.queryItemValue(QStringLiteral("product_key")), QStringLiteral("feichuan_offline_doudizhu"));
    QCOMPARE(query.queryItemValue(QStringLiteral("platform")), QStringLiteral("windows"));
    QCOMPARE(query.queryItemValue(QStringLiteral("channel")), QStringLiteral("stable"));
    QCOMPARE(query.queryItemValue(QStringLiteral("current_version")), QStringLiteral("1.0"));
}

void TestUpdateService::testParseAvailableUpdateRequiresSignedFields() {
    QJsonObject data;
    data[QStringLiteral("update_available")] = true;
    data[QStringLiteral("latest_version")] = QStringLiteral("1.1");
    data[QStringLiteral("release_notes")] = QString::fromUtf8(u8"更新说明");
    data[QStringLiteral("download_url")] = QStringLiteral("https://update.327802521.xyz/api/v1/updates/download/8/");
    data[QStringLiteral("file_size")] = 12345;
    data[QStringLiteral("sha256")] = QString::fromLatin1(fixedSha256());
    data[QStringLiteral("signature_algorithm")] = QStringLiteral("rsa-pkcs1-sha256");
    data[QStringLiteral("signature_payload_version")] = 1;
    data[QStringLiteral("signing_key_id")] = QString::fromLatin1(fixedSha256());
    data[QStringLiteral("signature")] = QString::fromLatin1(QByteArray(384, 'A').toBase64());
    QJsonObject root;
    root[QStringLiteral("code")] = 0;
    root[QStringLiteral("message")] = QStringLiteral("ok");
    root[QStringLiteral("data")] = data;
    const auto result = UpdateService::parseCheckResponse(QJsonDocument(root).toJson(QJsonDocument::Compact));
    QVERIFY(result.success);
    QVERIFY(result.updateAvailable);
    QCOMPARE(result.latestVersion, QStringLiteral("1.1"));
    QCOMPARE(result.fileSize, 12345);
    QCOMPARE(result.downloadUrl.scheme(), QStringLiteral("https"));
    QCOMPARE(result.sha256.size(), 64);
    QCOMPARE(result.signature.size(), 384);
}

void TestUpdateService::testParseNoUpdate() {
    const auto result = UpdateService::parseCheckResponse(R"({"code":0,"message":"ok","data":{"update_available":false}})");
    QVERIFY(result.success);
    QVERIFY(!result.updateAvailable);
}

void TestUpdateService::testRejectsUnsafeOrIncompleteUpdate() {
    const auto result = UpdateService::parseCheckResponse(R"({"code":0,"message":"ok","data":{"update_available":true,"latest_version":"1.1","download_url":"http://example.com/file.exe","file_size":1,"sha256":"bad"}})");
    QVERIFY(!result.success);
    QVERIFY(!result.errorMessage.isEmpty());
}

void TestUpdateService::testCanonicalSignaturePayload() {
    const QByteArray expected =
        "FPDZ-UPDATE-SIGNATURE-V1\n"
        "product_key=feichuan_offline_doudizhu\n"
        "platform=windows\n"
        "channel=stable\n"
        "version=1.0\n"
        "file_size=123456\n"
        "sha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n";
    QCOMPARE(buildReleaseSignaturePayload(fixedSignatureData()), expected);
}

void TestUpdateService::testProductionPublicKeyMatchesTrackedKeyId() {
    const QByteArray publicKey = readSourceFile(QStringLiteral("assets/update/release-signing-public.pem"));
    const QString expectedKeyId = QString::fromUtf8(readSourceFile(QStringLiteral("assets/update/release-signing-key-id.txt"))).trimmed();
    QVERIFY(!publicKey.isEmpty());
    QVERIFY(!expectedKeyId.isEmpty());
    QCOMPARE(releaseSigningKeyId(publicKey), expectedKeyId);
}

void TestUpdateService::testPinnedReleaseSignatureAndTamperRejection() {
    const QByteArray publicKey = readSourceFile(QStringLiteral("tests/updater/data/release_signing_test_public.pem"));
    const QByteArray encodedSignature = readSourceFile(QStringLiteral("tests/updater/data/release_signature_valid.b64")).trimmed();
    QVERIFY2(!publicKey.isEmpty(), "missing release signing public key");
    QVERIFY2(!encodedSignature.isEmpty(), "missing release signature test vector");
    ReleaseSignatureData data = fixedSignatureData();
    data.signingKeyId = releaseSigningKeyId(publicKey);
    data.signature = QByteArray::fromBase64(encodedSignature, QByteArray::AbortOnBase64DecodingErrors);
    QString error;
    QVERIFY2(verifyReleaseSignature(data, publicKey, &error), qPrintable(error));
    data.version = QStringLiteral("1.1");
    QVERIFY(!verifyReleaseSignature(data, publicKey, &error));
}

QTEST_MAIN(TestUpdateService)
#include "test_update_service.moc"
