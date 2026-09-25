#include "ai_service_client.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTimer>
#include <QUuid>

namespace fpdz {

AiServiceClient::AiServiceClient(QObject* parent) : QObject(parent) {
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&m_process, &QProcess::readyReadStandardOutput,
            this, &AiServiceClient::readOutput);
    connect(&m_process, &QProcess::readyReadStandardError, this, [this]() {
        m_process.readAllStandardError();
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &AiServiceClient::processFinished);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_process.state() == QProcess::NotRunning && !m_ready) {
            m_lastError = QString::fromUtf8(u8"无法启动飞船斗地主AI服务");
            emit serviceFailed(m_lastError);
        }
    });
}

AiServiceClient::~AiServiceClient() { stop(); }

bool AiServiceClient::start() {
    if (m_process.state() != QProcess::NotRunning) return true;
    m_ready = false;
    m_lastError.clear();
    m_buffer.clear();
    const QString fileName = QDir(QCoreApplication::applicationDirPath()).filePath(
        QString::fromUtf8(u8"飞船斗地主AI服务.exe"));
    if (!QFileInfo::exists(fileName)) {
        m_lastError = QString::fromUtf8(u8"未找到飞船斗地主AI服务.exe");
        emit serviceFailed(m_lastError);
        return false;
    }
    m_process.setProgram(fileName);
    m_process.start(QIODevice::ReadWrite);
    if (!m_process.waitForStarted(3000)) {
        m_lastError = QString::fromUtf8(u8"飞船斗地主AI服务启动失败");
        emit serviceFailed(m_lastError);
        return false;
    }
    return true;
}

void AiServiceClient::stop() {
    if (m_process.state() == QProcess::NotRunning) return;
    m_process.closeWriteChannel();
    if (!m_process.waitForFinished(1000)) {
        m_process.terminate();
        if (!m_process.waitForFinished(1000)) m_process.kill();
        m_process.waitForFinished(1000);
    }
    m_ready = false;
}

bool AiServiceClient::isRunning() const {
    return m_process.state() != QProcess::NotRunning;
}

bool AiServiceClient::send(const QJsonObject& message) {
    if (!isRunning()) return false;
    const QByteArray line = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    if (line.size() > AI_MAX_MESSAGE_BYTES || m_process.write(line) != line.size()) {
        m_lastError = QString::fromUtf8(u8"无法向AI服务发送请求");
        return false;
    }
    return true;
}

bool AiServiceClient::requestDecision(const AiDecisionRequest& request) {
    return send(request.toServiceJson());
}

void AiServiceClient::cancel(const QString& requestId) {
    send({{QStringLiteral("protocol_version"), AI_SERVICE_PROTOCOL_VERSION},
          {QStringLiteral("type"), QStringLiteral("cancel")},
          {QStringLiteral("request_id"), requestId}});
}

QJsonObject AiServiceClient::requestSync(QJsonObject message, int timeoutMilliseconds) {
    if (!isRunning() && !start()) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"), m_lastError}};
    }
    if (message.value("request_id").toString().isEmpty()) {
        message[QStringLiteral("request_id")] =
            QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    message[QStringLiteral("protocol_version")] = AI_SERVICE_PROTOCOL_VERSION;
    const QString requestId = message.value("request_id").toString();
    QJsonObject response;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    const auto connection = connect(this, &AiServiceClient::messageReceived, &loop,
        [&](const QJsonObject& received) {
            if (received.value("request_id").toString() != requestId) return;
            response = received;
            loop.quit();
        });
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    if (!send(message)) {
        disconnect(connection);
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"), m_lastError}};
    }
    timer.start(timeoutMilliseconds);
    loop.exec();
    disconnect(connection);
    if (response.isEmpty()) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"), QString::fromUtf8(u8"AI服务响应超时")}};
    }
    return response;
}

void AiServiceClient::readOutput() {
    m_buffer += m_process.readAllStandardOutput();
    if (m_buffer.size() > AI_MAX_MESSAGE_BYTES * 2) {
        m_buffer.clear();
        m_lastError = QString::fromUtf8(u8"AI服务输出超过协议限制");
        emit serviceFailed(m_lastError);
        return;
    }
    while (true) {
        const qsizetype newline = m_buffer.indexOf('\n');
        if (newline < 0) break;
        const QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        if (line.size() > AI_MAX_MESSAGE_BYTES) {
            emit serviceFailed(QString::fromUtf8(u8"AI服务报文超过128 KiB"));
            continue;
        }
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            emit serviceFailed(QString::fromUtf8(u8"AI服务返回损坏的JSON"));
            continue;
        }
        const QJsonObject message = document.object();
        if (message.value("protocol_version").toInt() != AI_SERVICE_PROTOCOL_VERSION) {
            emit serviceFailed(QString::fromUtf8(u8"AI服务协议版本不匹配"));
            continue;
        }
        if (message.value("type").toString() == QStringLiteral("ready")) {
            m_ready = message.value("ok").toBool();
            if (!m_ready) m_lastError = message.value("message").toString();
            emit readyChanged(m_ready);
        }
        emit messageReceived(message);
    }
}

void AiServiceClient::processFinished(int, QProcess::ExitStatus) {
    const bool wasReady = m_ready;
    m_ready = false;
    emit readyChanged(false);
    if (wasReady) emit serviceFailed(QString::fromUtf8(u8"AI服务意外退出"));
}

} // namespace fpdz
