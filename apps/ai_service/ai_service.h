#pragma once

#include "credential_store.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>

class QNetworkReply;

namespace fpdz::ai_service {

class AiService final : public QObject {
    Q_OBJECT
public:
    explicit AiService(QString credentialPath, QObject* parent = nullptr);
    void start();

public slots:
    void handleLine(const QByteArray& line);

signals:
    void outputLine(const QByteArray& line);

private:
    struct PendingRequest {
        QJsonObject context;
        QPointer<QNetworkReply> reply;
        QTimer* timer = nullptr;
        QElapsedTimer elapsed;
        QByteArray response;
        int protocol = 0;
        bool timedOut = false;
    };

    void handleDecision(const QJsonObject& message);
    void handleCredentialList(const QJsonObject& message);
    void handleCredentialSave(const QJsonObject& message);
    void handleCredentialDelete(const QJsonObject& message);
    void handleFetchModels(const QJsonObject& message);
    void startHttpRequest(const QJsonObject& context, const QJsonObject& credential,
                          const QUrl& url, const QByteArray& method,
                          const QByteArray& body, int protocol);
    void finishHttpRequest(const QString& requestId);
    void fail(const QJsonObject& context, const QString& code, const QString& message,
              int httpStatus = 0);
    void send(QJsonObject message);
    static QUrl requestUrl(const QJsonObject& credential, int protocol);
    static QUrl modelsUrl(const QJsonObject& credential);
    static QByteArray decisionBody(const QJsonObject& message,
                                   const QJsonObject& credential, int protocol);
    static QString extractDecisionText(const QJsonObject& response, int protocol,
                                       qint64* inputTokens, qint64* outputTokens,
                                       bool* reasoningOnly = nullptr);
    static void applyAuthentication(QNetworkRequest& request,
                                    const QJsonObject& credential);

    CredentialStore m_credentials;
    QNetworkAccessManager m_network;
    QHash<QString, PendingRequest> m_pending;
    bool m_storeReady = false;
};

} // namespace fpdz::ai_service
