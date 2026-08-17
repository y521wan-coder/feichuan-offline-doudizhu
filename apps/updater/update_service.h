#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QObject>

class QNetworkReply;
class QTimer;

namespace fpdz {

struct UpdateCheckResult {
    bool success = false;
    bool updateAvailable = false;
    QString latestVersion;
    QString releaseNotes;
    QUrl downloadUrl;
    qint64 fileSize = 0;
    QByteArray sha256;
    QString errorMessage;
};

class UpdateService : public QObject {
    Q_OBJECT
public:
    explicit UpdateService(QObject* parent = nullptr);

    void checkForUpdates(const QString& currentVersion);
    void downloadAndVerify(const UpdateCheckResult& update);
    void cancel();

    static QUrl buildCheckUrl(const QString& currentVersion);
    static UpdateCheckResult parseCheckResponse(const QByteArray& payload);

signals:
    void checkFinished(const fpdz::UpdateCheckResult& result);
    void downloadProgress(qint64 received, qint64 total);
    void downloadFinished(const QString& installerPath, const QString& errorMessage);

private:
    void finishDownloadWithError(const QString& errorMessage);
    void clearDownloadState(bool removeFile);

    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_checkReply;
    QPointer<QNetworkReply> m_downloadReply;
    QTimer* m_checkTimeout = nullptr;
    QTimer* m_downloadTimeout = nullptr;
    QFile m_downloadFile;
    QCryptographicHash m_downloadHash{QCryptographicHash::Sha256};
    QByteArray m_expectedSha256;
    QString m_downloadPath;
};

} // namespace fpdz

Q_DECLARE_METATYPE(fpdz::UpdateCheckResult)
