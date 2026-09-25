#pragma once

#include "../ai/ai_decision_request.h"

#include <QJsonObject>
#include <QObject>
#include <QProcess>

namespace fpdz {

class AiServiceClient final : public QObject {
    Q_OBJECT
public:
    explicit AiServiceClient(QObject* parent = nullptr);
    ~AiServiceClient() override;

    bool start();
    void stop();
    bool isRunning() const;
    bool isReady() const { return m_ready; }
    QString lastError() const { return m_lastError; }
    bool send(const QJsonObject& message);
    bool requestDecision(const AiDecisionRequest& request);
    void cancel(const QString& requestId);
    QJsonObject requestSync(QJsonObject message, int timeoutMilliseconds = 5000);

signals:
    void readyChanged(bool ready);
    void messageReceived(const QJsonObject& message);
    void serviceFailed(const QString& safeMessage);

private slots:
    void readOutput();
    void processFinished(int exitCode, QProcess::ExitStatus status);

private:
    QProcess m_process;
    QByteArray m_buffer;
    bool m_ready = false;
    QString m_lastError;
};

} // namespace fpdz
