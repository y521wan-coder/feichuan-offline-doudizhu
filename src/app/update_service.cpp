#include "update_service.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>

namespace fpdz {
namespace {

constexpr auto kUpdateEndpoint = "https://update.327802521.xyz/api/v1/updates/check";
constexpr auto kProductKey = "feichuan_ai_doudizhu";
constexpr auto kPlatform = "windows";
constexpr auto kChannel = "stable";
constexpr int kCheckTimeoutMilliseconds = 15000;
constexpr int kDownloadTimeoutMilliseconds = 120000;

QString responseError(const QJsonObject& root, const QString& fallback) {
    const QString message = root.value(QStringLiteral("message")).toString().trimmed();
    return message.isEmpty() ? fallback : message;
}

} // namespace

UpdateService::UpdateService(QObject* parent) : QObject(parent) {
    qRegisterMetaType<UpdateCheckResult>();
    m_checkTimeout = new QTimer(this);
    m_checkTimeout->setSingleShot(true);
    connect(m_checkTimeout, &QTimer::timeout, this, [this]() {
        if (m_checkReply) m_checkReply->abort();
    });
    m_downloadTimeout = new QTimer(this);
    m_downloadTimeout->setSingleShot(true);
    connect(m_downloadTimeout, &QTimer::timeout, this, [this]() {
        if (m_downloadReply) m_downloadReply->abort();
    });
}

QUrl UpdateService::buildCheckUrl(const QString& currentVersion) {
    QUrl url(QString::fromLatin1(kUpdateEndpoint));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("product_key"), QString::fromLatin1(kProductKey));
    query.addQueryItem(QStringLiteral("platform"), QString::fromLatin1(kPlatform));
    query.addQueryItem(QStringLiteral("channel"), QString::fromLatin1(kChannel));
    query.addQueryItem(QStringLiteral("current_version"), currentVersion);
    url.setQuery(query);
    return url;
}

UpdateCheckResult UpdateService::parseCheckResponse(const QByteArray& payload) {
    UpdateCheckResult result;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.errorMessage = QString::fromUtf8(u8"更新服务器返回了无法识别的数据。");
        return result;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("code")).toInt(-1) != 0) {
        result.errorMessage = responseError(root, QString::fromUtf8(u8"更新服务器拒绝了请求。"));
        return result;
    }
    const QJsonValue dataValue = root.value(QStringLiteral("data"));
    if (!dataValue.isObject()) {
        result.errorMessage = QString::fromUtf8(u8"更新服务器没有返回版本数据。");
        return result;
    }

    const QJsonObject data = dataValue.toObject();
    result.updateAvailable = data.value(QStringLiteral("update_available")).toBool(false);
    result.latestVersion = data.value(QStringLiteral("latest_version")).toString().trimmed();
    result.releaseNotes = data.value(QStringLiteral("release_notes")).toString();
    result.fileSize = data.value(QStringLiteral("file_size")).toVariant().toLongLong();
    if (!result.updateAvailable) {
        result.success = true;
        return result;
    }

    result.downloadUrl = QUrl(data.value(QStringLiteral("download_url")).toString().trimmed());
    result.sha256 = data.value(QStringLiteral("sha256")).toString().trimmed().toLatin1().toLower();
    static const QRegularExpression shaPattern(QStringLiteral("^[0-9a-fA-F]{64}$"));
    if (result.latestVersion.isEmpty() || !result.downloadUrl.isValid() ||
        result.downloadUrl.scheme() != QStringLiteral("https") ||
        !shaPattern.match(QString::fromLatin1(result.sha256)).hasMatch()) {
        result.errorMessage = QString::fromUtf8(u8"更新信息不完整或不安全，已停止更新。");
        return result;
    }
    result.success = true;
    return result;
}

void UpdateService::checkForUpdates(const QString& currentVersion) {
    if (m_checkReply) return;
    QNetworkRequest request(buildCheckUrl(currentVersion));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("FeichuanAiDoudizhu/%1").arg(currentVersion));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    m_checkReply = m_network.get(request);
    m_checkTimeout->start(kCheckTimeoutMilliseconds);
    connect(m_checkReply, &QNetworkReply::finished, this, [this]() {
        m_checkTimeout->stop();
        UpdateCheckResult result;
        if (!m_checkReply) {
            result.errorMessage = QString::fromUtf8(u8"更新检查已取消。");
        } else if (m_checkReply->error() != QNetworkReply::NoError) {
            result.errorMessage = QString::fromUtf8(u8"无法连接更新服务器：") +
                                  m_checkReply->errorString();
        } else {
            result = parseCheckResponse(m_checkReply->readAll());
        }
        if (m_checkReply) m_checkReply->deleteLater();
        m_checkReply = nullptr;
        emit checkFinished(result);
    });
}

void UpdateService::downloadAndVerify(const UpdateCheckResult& update) {
    if (m_downloadReply) return;
    if (!update.success || !update.updateAvailable ||
        update.downloadUrl.scheme() != QStringLiteral("https") ||
        update.sha256.size() != 64) {
        emit downloadFinished({}, QString::fromUtf8(u8"更新信息无效，不能下载安装包。"));
        return;
    }

    const QString directory = QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
                              QStringLiteral("/FeichuanAiDoudizhuUpdates");
    if (!QDir().mkpath(directory)) {
        emit downloadFinished({}, QString::fromUtf8(u8"无法创建更新临时目录。"));
        return;
    }
    m_downloadPath = directory + QString::fromUtf8(u8"/飞船AI斗地主单机版-Setup-") +
                     update.latestVersion + QStringLiteral(".exe");
    m_downloadFile.setFileName(m_downloadPath);
    if (!m_downloadFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit downloadFinished({}, QString::fromUtf8(u8"无法写入更新安装包。"));
        clearDownloadState(true);
        return;
    }
    m_downloadHash.reset();
    m_expectedSha256 = update.sha256.toLower();

    QNetworkRequest request(update.downloadUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("FeichuanAiDoudizhu/%1")
                          .arg(QCoreApplication::applicationVersion()));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    m_downloadReply = m_network.get(request);
    m_downloadTimeout->start(kDownloadTimeoutMilliseconds);
    connect(m_downloadReply, &QNetworkReply::downloadProgress,
            this, &UpdateService::downloadProgress);
    connect(m_downloadReply, &QIODevice::readyRead, this, [this]() {
        if (!m_downloadReply) return;
        const QByteArray chunk = m_downloadReply->readAll();
        if (chunk.isEmpty()) return;
        if (m_downloadFile.write(chunk) != chunk.size()) {
            m_downloadReply->abort();
            return;
        }
        m_downloadHash.addData(chunk);
        m_downloadTimeout->start(kDownloadTimeoutMilliseconds);
    });
    connect(m_downloadReply, &QNetworkReply::finished, this, [this]() {
        m_downloadTimeout->stop();
        if (m_downloadReply) {
            const QByteArray remaining = m_downloadReply->readAll();
            if (!remaining.isEmpty()) {
                if (m_downloadFile.write(remaining) != remaining.size()) {
                    finishDownloadWithError(QString::fromUtf8(u8"写入更新安装包失败。"));
                    return;
                }
                m_downloadHash.addData(remaining);
            }
        }
        m_downloadFile.close();
        if (!m_downloadReply || m_downloadReply->error() != QNetworkReply::NoError) {
            const QString detail = m_downloadReply ? m_downloadReply->errorString() : QString();
            finishDownloadWithError(QString::fromUtf8(u8"更新安装包下载失败：") + detail);
            return;
        }
        const QByteArray actualSha256 = m_downloadHash.result().toHex().toLower();
        if (actualSha256 != m_expectedSha256) {
            finishDownloadWithError(QString::fromUtf8(u8"更新安装包 SHA-256 校验失败，已禁止执行。"));
            return;
        }
        const QString completedPath = m_downloadPath;
        clearDownloadState(false);
        emit downloadFinished(completedPath, {});
    });
}

void UpdateService::finishDownloadWithError(const QString& errorMessage) {
    clearDownloadState(true);
    emit downloadFinished({}, errorMessage);
}

void UpdateService::clearDownloadState(bool removeFile) {
    if (m_downloadFile.isOpen()) m_downloadFile.close();
    if (m_downloadReply) m_downloadReply->deleteLater();
    m_downloadReply = nullptr;
    if (removeFile && !m_downloadPath.isEmpty()) QFile::remove(m_downloadPath);
    m_downloadPath.clear();
    m_expectedSha256.clear();
    m_downloadHash.reset();
}

void UpdateService::cancel() {
    if (m_checkReply) m_checkReply->abort();
    if (m_downloadReply) m_downloadReply->abort();
}

} // namespace fpdz
