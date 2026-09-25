#include "credential_store.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace fpdz::ai_service {
namespace {

QByteArray protect(const QByteArray& plain, QString* error) {
#ifdef Q_OS_WIN
    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.constData()));
    input.cbData = static_cast<DWORD>(plain.size());
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"飞船斗地主AI认证", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        if (error) *error = QString::fromUtf8(u8"无法使用Windows用户凭据保护认证数据");
        return {};
    }
    QByteArray encrypted(reinterpret_cast<const char*>(output.pbData),
                         static_cast<qsizetype>(output.cbData));
    LocalFree(output.pbData);
    return encrypted;
#else
    Q_UNUSED(error);
    return plain;
#endif
}

QByteArray unprotect(const QByteArray& encrypted, QString* error) {
#ifdef Q_OS_WIN
    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(encrypted.constData()));
    input.cbData = static_cast<DWORD>(encrypted.size());
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        if (error) *error = QString::fromUtf8(u8"认证数据无法由当前Windows用户解密");
        return {};
    }
    QByteArray plain(reinterpret_cast<const char*>(output.pbData),
                     static_cast<qsizetype>(output.cbData));
    LocalFree(output.pbData);
    return plain;
#else
    Q_UNUSED(error);
    return encrypted;
#endif
}

bool validProtocol(int value) { return value >= 0 && value <= 2; }
bool validAuth(int value) { return value >= 0 && value <= 4; }

} // namespace

CredentialStore::CredentialStore(QString filePath) : m_filePath(std::move(filePath)) {}

bool CredentialStore::load(QString* error) {
    QFile file(m_filePath);
    if (!file.exists()) {
        m_records = {};
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QString::fromUtf8(u8"无法读取认证文件");
        return false;
    }
    const QByteArray encrypted = file.readAll();
    const QByteArray plain = unprotect(encrypted, error);
    if (plain.isEmpty() && !encrypted.isEmpty()) return false;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(plain, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
        !document.object().value("records").isArray()) {
        if (error) *error = QString::fromUtf8(u8"认证文件格式损坏，旧文件未被覆盖");
        return false;
    }
    m_records = document.object().value("records").toArray();
    return true;
}

bool CredentialStore::save(QString* error) const {
    const QByteArray plain = QJsonDocument(QJsonObject{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("records"), m_records}}).toJson(QJsonDocument::Compact);
    const QByteArray encrypted = protect(plain, error);
    if (encrypted.isEmpty() && !plain.isEmpty()) return false;
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(encrypted) != encrypted.size() ||
        !file.commit()) {
        if (error) *error = QString::fromUtf8(u8"认证数据原子写入失败");
        return false;
    }
    return true;
}

QJsonArray CredentialStore::publicRecords() const {
    QJsonArray result;
    for (const auto& value : m_records) {
        QJsonObject record = value.toObject();
        record.remove(QStringLiteral("api_key"));
        record.remove(QStringLiteral("custom_header_value"));
        record[QStringLiteral("has_secret")] =
            !value.toObject().value("api_key").toString().isEmpty() ||
            !value.toObject().value("custom_header_value").toString().isEmpty();
        result.append(record);
    }
    return result;
}

QJsonObject CredentialStore::record(const QString& id) const {
    for (const auto& value : m_records) {
        if (value.toObject().value("id").toString() == id) return value.toObject();
    }
    return {};
}

bool CredentialStore::upsert(QJsonObject value, bool clearSecret, QString* id,
                             QString* error) {
    QString recordId = value.value("id").toString().trimmed();
    if (recordId.isEmpty()) recordId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString name = value.value("name").toString().trimmed();
    const int protocol = value.value("protocol").toInt(-1);
    const int auth = value.value("auth_method").toInt(-1);
    const QString requestUrl = value.value("request_url").toString().trimmed();
    if (name.isEmpty() || !validProtocol(protocol) || !validAuth(auth) ||
        requestUrl.isEmpty()) {
        if (error) *error = QString::fromUtf8(u8"认证名称、协议或请求地址无效");
        return false;
    }
    const QString headerName = value.value("custom_header_name").toString().trimmed();
    if (auth == 3 && !headerNameAllowed(headerName)) {
        if (error) *error = QString::fromUtf8(u8"自定义请求头名称不安全或不受支持");
        return false;
    }
    const QString apiKey = value.value("api_key").toString();
    const QString customValue = value.value("custom_header_value").toString();
    if (apiKey.contains('\r') || apiKey.contains('\n') ||
        customValue.contains('\r') || customValue.contains('\n')) {
        if (error) *error = QString::fromUtf8(u8"认证值不能包含换行符");
        return false;
    }

    int found = -1;
    QJsonObject old;
    for (int index = 0; index < m_records.size(); ++index) {
        if (m_records[index].toObject().value("id").toString() == recordId) {
            found = index;
            old = m_records[index].toObject();
            break;
        }
    }
    value[QStringLiteral("id")] = recordId;
    if (protocol == 0) {
        value[QStringLiteral("request_url")] = QStringLiteral("https://api.openai.com/v1/responses");
        value[QStringLiteral("models_url")] = QStringLiteral("https://api.openai.com/v1/models");
        value[QStringLiteral("auth_method")] = 0;
    }
    if (clearSecret) {
        value[QStringLiteral("api_key")] = QString();
        value[QStringLiteral("custom_header_value")] = QString();
    } else {
        if (value.value("api_key").toString().isEmpty()) {
            value[QStringLiteral("api_key")] = old.value("api_key").toString();
        }
        if (value.value("custom_header_value").toString().isEmpty()) {
            value[QStringLiteral("custom_header_value")] =
                old.value("custom_header_value").toString();
        }
    }
    if (found >= 0) m_records[found] = value;
    else m_records.append(value);
    if (!save(error)) {
        if (found >= 0) m_records[found] = old;
        else m_records.removeLast();
        return false;
    }
    if (id) *id = recordId;
    return true;
}

bool CredentialStore::remove(const QString& id, QString* error) {
    for (int index = 0; index < m_records.size(); ++index) {
        if (m_records[index].toObject().value("id").toString() != id) continue;
        const QJsonValue old = m_records.takeAt(index);
        if (!save(error)) {
            m_records.insert(index, old);
            return false;
        }
        return true;
    }
    if (error) *error = QString::fromUtf8(u8"认证不存在");
    return false;
}

bool CredentialStore::headerNameAllowed(const QString& name) {
    const QString normalized = name.trimmed().toLower();
    static const QSet<QString> forbidden{
        QStringLiteral("host"), QStringLiteral("content-length"),
        QStringLiteral("connection"), QStringLiteral("proxy-connection"),
        QStringLiteral("keep-alive"), QStringLiteral("transfer-encoding"),
        QStringLiteral("te"), QStringLiteral("trailer"),
        QStringLiteral("upgrade"), QStringLiteral("proxy-authorization")};
    if (normalized.isEmpty() || forbidden.contains(normalized)) {
        return false;
    }
    for (const QChar character : normalized) {
        if (character.unicode() > 127 ||
            !(character.isLetterOrNumber() || character == QLatin1Char('-'))) return false;
    }
    return true;
}

} // namespace fpdz::ai_service
