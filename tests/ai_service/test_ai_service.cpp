#include <QtTest>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include <algorithm>
#include <vector>

#include "../../apps/ai_service/credential_store.h"

using fpdz::ai_service::CredentialStore;

namespace {

class FakeHttpServer final : public QObject {
public:
    explicit FakeHttpServer(QObject* parent = nullptr) : QObject(parent) {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                    m_buffers[socket] += socket->readAll();
                    const QByteArray request = m_buffers[socket];
                    const qsizetype headerEnd = request.indexOf("\r\n\r\n");
                    if (headerEnd < 0) return;
                    int contentLength = 0;
                    for (const QByteArray& line : request.left(headerEnd).split('\n')) {
                        if (line.toLower().startsWith("content-length:")) {
                            contentLength = line.mid(line.indexOf(':') + 1).trimmed().toInt();
                        }
                    }
                    if (request.size() < headerEnd + 4 + contentLength) return;
                    ++requestCount;
                    lastRequest = request;
                    QByteArray body;
                    QByteArray status = "200 OK";
                    QByteArray extraHeaders;
                    if (mode == 8) {
                        return;
                    } else if (mode == 7) {
                        body = QByteArray(129 * 1024, 'x');
                    } else if (mode == 6) {
                        status = "401 Unauthorized";
                        body = R"({"error":{"message":"secret server detail"}})";
                    } else if (mode == 5) {
                        status = "302 Found";
                        extraHeaders = "Location: http://example.invalid/steal\r\n";
                        body = "{}";
                    } else if (mode == 4) {
                        body = R"({"choices":[{"message":{"content":"I refuse"}}]})";
                    } else if (mode == 3) {
                        body = R"({"choices":[{"message":{"content":"{\"action_id\":1.5}"}}]})";
                    } else if (mode == 2) {
                        body = R"({"choices":[{"message":{"content":"{\"action_id\":0}"}}],"usage":{"prompt_tokens":4,"completion_tokens":2}})";
                    } else if (mode == -1) {
                        body = R"({"data":[{"id":"fake-a"},{"id":"fake-b"}]})";
                    } else {
                        body = R"({"output":[{"type":"message","content":[{"type":"output_text","text":"{\"action_id\":0}"}]}],"usage":{"input_tokens":4,"output_tokens":2}})";
                    }
                    const QByteArray response = "HTTP/1.1 " + status +
                        "\r\nContent-Type: application/json\r\n" + extraHeaders +
                        "Content-Length: " + QByteArray::number(body.size()) +
                        "\r\nConnection: close\r\n\r\n" + body;
                    socket->write(response);
                    socket->disconnectFromHost();
                    m_buffers.remove(socket);
                });
            }
        });
    }

    bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
    QString rootUrl() const {
        return QStringLiteral("http://127.0.0.1:%1/v1").arg(m_server.serverPort());
    }

    int mode = 0;
    int requestCount = 0;
    QByteArray lastRequest;

private:
    QTcpServer m_server;
    QHash<QTcpSocket*, QByteArray> m_buffers;
};

QJsonObject readMessage(QProcess& process, int timeoutMs = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (process.canReadLine()) {
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(process.readLine().trimmed(), &error);
            if (error.error == QJsonParseError::NoError && document.isObject()) {
                return document.object();
            }
        }
        process.waitForReadyRead(10);
    }
    return {};
}

void writeMessage(QProcess& process, const QJsonObject& object) {
    const QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    QCOMPARE(process.write(line), line.size());
    QVERIFY(process.waitForBytesWritten(1000));
}

} // namespace

class TestAiService : public QObject {
    Q_OBJECT
private slots:
    void testCredentialStoreEncryptsWholeRecordAndNeverEchoesSecret() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("credentials.dat"));
        CredentialStore store(path);
        QVERIFY(store.load());
        QJsonObject credential{{QStringLiteral("name"), QStringLiteral("local")},
                               {QStringLiteral("protocol"), 1},
                               {QStringLiteral("request_url"), QStringLiteral("http://127.0.0.1/v1")},
                               {QStringLiteral("auth_method"), 0},
                               {QStringLiteral("api_key"), QStringLiteral("super-secret-value")}};
        QString id;
        QString error;
        QVERIFY2(store.upsert(credential, false, &id, &error), qPrintable(error));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray encrypted = file.readAll();
        QVERIFY(!encrypted.contains("super-secret-value"));
        QVERIFY(!encrypted.contains("http://127.0.0.1"));

        CredentialStore reloaded(path);
        QVERIFY2(reloaded.load(&error), qPrintable(error));
        QCOMPARE(reloaded.record(id).value("api_key").toString(),
                 QStringLiteral("super-secret-value"));
        const auto publicRecord = reloaded.publicRecords().at(0).toObject();
        QVERIFY(!publicRecord.contains("api_key"));
        QVERIFY(publicRecord.value("has_secret").toBool());

        QVERIFY(!CredentialStore::headerNameAllowed(QStringLiteral("Host")));
        QVERIFY(!CredentialStore::headerNameAllowed(QStringLiteral("Transfer-Encoding")));
        QVERIFY(!CredentialStore::headerNameAllowed(QStringLiteral("Proxy-Connection")));
        QVERIFY(!CredentialStore::headerNameAllowed(QString::fromUtf8(u8"密钥")));
        QVERIFY(CredentialStore::headerNameAllowed(QStringLiteral("X-Custom-Token")));
        QJsonObject injected = credential;
        injected[QStringLiteral("id")] = id;
        injected[QStringLiteral("api_key")] = QStringLiteral("value\r\nX-Evil: yes");
        QVERIFY(!store.upsert(injected, false, nullptr, &error));
    }

    void testThreeProtocolsAndModelListingThroughVersionedJsonIpc() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        FakeHttpServer server;
        QVERIFY(server.listen());
        QProcess process;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("FPDZ_AI_TEST_OPENAI_BASE_URL"), server.rootUrl());
        process.setProcessEnvironment(environment);
        const QString executable = QCoreApplication::applicationDirPath() +
            QStringLiteral("/ai_service_test_host.exe");
        process.start(executable, {QStringLiteral("--credentials-file"),
                                   directory.filePath(QStringLiteral("credentials.dat"))});
        QVERIFY(process.waitForStarted(3000));
        const auto ready = readMessage(process);
        QCOMPARE(ready.value("type").toString(), QStringLiteral("ready"));
        QVERIFY(ready.value("ok").toBool());

        QString lastCredentialId;
        for (int protocol = 0; protocol <= 2; ++protocol) {
            const int storedProtocol = protocol;
            const QString saveId = QStringLiteral("save-%1").arg(protocol);
            writeMessage(process, {
                {QStringLiteral("protocol_version"), 1},
                {QStringLiteral("type"), QStringLiteral("credential_save")},
                {QStringLiteral("request_id"), saveId},
                {QStringLiteral("credential"), QJsonObject{
                    {QStringLiteral("name"), QStringLiteral("fake-%1").arg(protocol)},
                    {QStringLiteral("protocol"), storedProtocol},
                    {QStringLiteral("request_url"), server.rootUrl()},
                    {QStringLiteral("auth_method"), 4}}}});
            const auto saved = readMessage(process);
            QVERIFY(saved.value("ok").toBool());
            const QString credentialId = saved.value("credential_id").toString();
            QVERIFY(!credentialId.isEmpty());
            lastCredentialId = credentialId;

            server.mode = storedProtocol;
            const QString requestId = QStringLiteral("decision-%1").arg(protocol);
            writeMessage(process, {
                {QStringLiteral("protocol_version"), 1},
                {QStringLiteral("type"), QStringLiteral("decision")},
                {QStringLiteral("request_id"), requestId},
                {QStringLiteral("game_id"), 7},
                {QStringLiteral("event_sequence"), 9},
                {QStringLiteral("phase"), QStringLiteral("playing")},
                {QStringLiteral("seat"), 1},
                {QStringLiteral("credential_id"), credentialId},
                {QStringLiteral("model"), QStringLiteral("fake-model")},
                {QStringLiteral("strength"), QStringLiteral("medium")},
                {QStringLiteral("timeout_ms"), 3000},
                {QStringLiteral("strategy_prompt"), protocol == 1
                    ? QString::fromUtf8(u8"优先观察农民配合，避免浪费高牌。") : QString{}},
                {QStringLiteral("payload"), QJsonObject{
                    {QStringLiteral("player_count"), protocol + 2},
                    {QStringLiteral("legal_actions"), QJsonArray{
                        QJsonObject{{QStringLiteral("action_id"), 0}}}}}}});
            const auto decision = readMessage(process, 6000);
            QCOMPARE(decision.value("request_id").toString(), requestId);
            QVERIFY(decision.value("ok").toBool());
            QCOMPARE(decision.value("action_id").toInt(), 0);

            const qsizetype bodyStart = server.lastRequest.indexOf("\r\n\r\n");
            QVERIFY(bodyStart >= 0);
            const QJsonObject httpBody = QJsonDocument::fromJson(
                server.lastRequest.mid(bodyStart + 4)).object();
            const QString instructions = protocol == 2
                ? httpBody.value("messages").toArray().at(0).toObject()
                      .value("content").toString()
                : httpBody.value("instructions").toString();
            QVERIFY(instructions.contains(QString::fromUtf8(u8"地主自己先出完即获胜")));
            QVERIFY(instructions.contains(QString::fromUtf8(u8"任意农民先出完则全体农民获胜")));
            if (protocol == 1) {
                QVERIFY(instructions.contains(QString::fromUtf8(u8"优先观察农民配合，避免浪费高牌。")));
                QVERIFY(!instructions.contains(QString::fromUtf8(u8"农民通常不压队友")));
            } else {
                QVERIFY(instructions.contains(QString::fromUtf8(u8"农民通常不压队友")));
            }
            if (protocol == 0) {
                QVERIFY(instructions.contains(QString::fromUtf8(u8"本局二人")));
                QVERIFY(instructions.contains(QString::fromUtf8(u8"盖牌不参与")));
                QVERIFY(instructions.contains(QString::fromUtf8(u8"王炸(14)")));
                QVERIFY(!instructions.contains(QString::fromUtf8(u8"本局三人")));
            } else if (protocol == 1) {
                QVERIFY(instructions.contains(QString::fromUtf8(u8"本局三人")));
                QVERIFY(instructions.contains(QString::fromUtf8(u8"任一农民出完则两农民同胜")));
                QVERIFY(!instructions.contains(QString::fromUtf8(u8"本局四人")));
            } else {
                QVERIFY(instructions.contains(QString::fromUtf8(u8"本局四人")));
                QVERIFY(instructions.contains(QString::fromUtf8(u8"14双王枪毙")));
                QVERIFY(instructions.contains(QString::fromUtf8(u8"19天尊无敌")));
                QVERIFY(instructions.contains(QString::fromUtf8(u8"飞机翅膀不能与主体同点")));
                QVERIFY(!instructions.contains(QString::fromUtf8(u8"王炸(14)")));
            }
            QVERIFY(instructions.contains(QString::fromUtf8(u8"rank从0到14依次是3,4,5")));
            QVERIFY(instructions.contains(QStringLiteral("legal_actions")));
            QVERIFY(instructions.contains(QStringLiteral("{\"action_id\":7}")));
        }

        server.mode = -1;
        writeMessage(process, {
            {QStringLiteral("protocol_version"), 1},
            {QStringLiteral("type"), QStringLiteral("fetch_models")},
            {QStringLiteral("request_id"), QStringLiteral("models-1")},
            {QStringLiteral("credential_id"), lastCredentialId},
            {QStringLiteral("timeout_ms"), 3000}});
        const auto models = readMessage(process, 6000);
        QVERIFY(models.value("ok").toBool());
        QCOMPARE(models.value("models").toArray().size(), 2);

        auto sendFaultDecision = [&](int mode, const QString& requestId,
                                     const QString& expectedCode, int timeout = 6000) {
            server.mode = mode;
            writeMessage(process, {
                {QStringLiteral("protocol_version"), 1},
                {QStringLiteral("type"), QStringLiteral("decision")},
                {QStringLiteral("request_id"), requestId},
                {QStringLiteral("game_id"), 7},
                {QStringLiteral("event_sequence"), 9},
                {QStringLiteral("phase"), QStringLiteral("playing")},
                {QStringLiteral("seat"), 1},
                {QStringLiteral("credential_id"), lastCredentialId},
                {QStringLiteral("model"), QStringLiteral("fake-model")},
                {QStringLiteral("timeout_ms"), 3000},
                {QStringLiteral("payload"), QJsonObject{
                    {QStringLiteral("legal_actions"), QJsonArray{
                        QJsonObject{{QStringLiteral("action_id"), 0}}}}}}});
            const auto result = readMessage(process, timeout);
            QCOMPARE(result.value("request_id").toString(), requestId);
            QVERIFY(!result.value("ok").toBool());
            QCOMPARE(result.value("error_code").toString(), expectedCode);
            QVERIFY(!result.value("message").toString().contains(
                QStringLiteral("secret server detail")));
        };
        sendFaultDecision(3, QStringLiteral("fractional"),
                          QStringLiteral("malformed_action_value"));
        sendFaultDecision(4, QStringLiteral("natural-language"),
                          QStringLiteral("invalid_model_json"));
        const int redirectsBefore = server.requestCount;
        sendFaultDecision(5, QStringLiteral("redirect"),
                          QStringLiteral("redirect_blocked"));
        QCOMPARE(server.requestCount, redirectsBefore + 1);
        sendFaultDecision(6, QStringLiteral("unauthorized"),
                          QStringLiteral("http_401"));
        sendFaultDecision(7, QStringLiteral("oversized"),
                          QStringLiteral("response_too_large"));
        QElapsedTimer timeoutTimer;
        timeoutTimer.start();
        sendFaultDecision(8, QStringLiteral("timeout"), QStringLiteral("timeout"), 7000);
        const qint64 timeoutElapsed = timeoutTimer.elapsed();
        QVERIFY2(qAbs(timeoutElapsed - 3000) <= 250,
                 qPrintable(QStringLiteral("Timeout error was %1 ms").arg(timeoutElapsed)));

        server.mode = 2;
        std::vector<qint64> latencies;
        latencies.reserve(1000);
        for (int index = 0; index < 1000; ++index) {
            QElapsedTimer decisionTimer;
            decisionTimer.start();
            const QString requestId = QStringLiteral("performance-%1").arg(index);
            writeMessage(process, {
                {QStringLiteral("protocol_version"), 1},
                {QStringLiteral("type"), QStringLiteral("decision")},
                {QStringLiteral("request_id"), requestId},
                {QStringLiteral("game_id"), 8},
                {QStringLiteral("event_sequence"), index},
                {QStringLiteral("phase"), QStringLiteral("playing")},
                {QStringLiteral("seat"), 1},
                {QStringLiteral("credential_id"), lastCredentialId},
                {QStringLiteral("model"), QStringLiteral("fake-model")},
                {QStringLiteral("timeout_ms"), 3000},
                {QStringLiteral("payload"), QJsonObject{
                    {QStringLiteral("legal_actions"), QJsonArray{
                        QJsonObject{{QStringLiteral("action_id"), 0}}}}}}});
            const auto result = readMessage(process, 3000);
            QVERIFY(result.value("ok").toBool());
            QCOMPARE(result.value("request_id").toString(), requestId);
            latencies.push_back(decisionTimer.elapsed());
        }
        std::sort(latencies.begin(), latencies.end());
        const qint64 p95 = latencies[949];
        qInfo("AI_SERVICE_PERF decisions=1000 p95_ms=%lld timeout_ms=%lld",
              static_cast<long long>(p95),
              static_cast<long long>(timeoutElapsed));
        QVERIFY2(p95 < 50,
                 qPrintable(QStringLiteral("IPC/HTTP fake decision P95 was %1 ms").arg(p95)));

        process.closeWriteChannel();
        QVERIFY(process.waitForFinished(3000));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    }
};

QTEST_MAIN(TestAiService)
#include "test_ai_service.moc"
