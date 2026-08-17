#include "signature_verifier.h"

#include <QCryptographicHash>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#endif

namespace fpdz {
namespace {

QByteArray publicKeyDer(const QByteArray& pem, QString* errorMessage) {
    QByteArray body = pem;
    body.replace("-----BEGIN PUBLIC KEY-----", "");
    body.replace("-----END PUBLIC KEY-----", "");
    body.replace("\r", "");
    body.replace("\n", "");
    body.replace(" ", "");
    body.replace("\t", "");
    const QByteArray der = QByteArray::fromBase64(body, QByteArray::AbortOnBase64DecodingErrors);
    if (der.isEmpty() && errorMessage) {
        *errorMessage = QString::fromUtf8(u8"发布公钥格式无效。");
    }
    return der;
}

bool validPayloadField(const QString& value) {
    return !value.isEmpty() && !value.contains(QLatin1Char('\n')) &&
           !value.contains(QLatin1Char('\r'));
}

} // namespace

QByteArray buildReleaseSignaturePayload(const ReleaseSignatureData& data) {
    QByteArray payload("FPDZ-UPDATE-SIGNATURE-V1\n");
    payload += "product_key=" + data.productKey.toUtf8() + '\n';
    payload += "platform=" + data.platform.toUtf8() + '\n';
    payload += "channel=" + data.channel.toUtf8() + '\n';
    payload += "version=" + data.version.toUtf8() + '\n';
    payload += "file_size=" + QByteArray::number(data.fileSize) + '\n';
    payload += "sha256=" + data.sha256.toLower() + '\n';
    return payload;
}

QString releaseSigningKeyId(const QByteArray& publicKeyPem, QString* errorMessage) {
    const QByteArray der = publicKeyDer(publicKeyPem, errorMessage);
    if (der.isEmpty()) return {};
    return QString::fromLatin1(QCryptographicHash::hash(der, QCryptographicHash::Sha256)
                                   .toHex().toLower());
}

bool verifyReleaseSignature(const ReleaseSignatureData& data,
                            const QByteArray& publicKeyPem,
                            QString* errorMessage) {
    static const QRegularExpression shaPattern(QStringLiteral("^[0-9a-f]{64}$"));
    if (!validPayloadField(data.productKey) || !validPayloadField(data.platform) ||
        !validPayloadField(data.channel) || !validPayloadField(data.version) ||
        data.fileSize <= 0 ||
        !shaPattern.match(QString::fromLatin1(data.sha256.toLower())).hasMatch() ||
        data.algorithm != QStringLiteral("rsa-pkcs1-sha256") ||
        data.payloadVersion != 1 || data.signature.isEmpty()) {
        if (errorMessage) *errorMessage = QString::fromUtf8(u8"发布签名数据不完整。");
        return false;
    }

    QString keyError;
    const QString actualKeyId = releaseSigningKeyId(publicKeyPem, &keyError);
    if (actualKeyId.isEmpty() ||
        actualKeyId.compare(data.signingKeyId, Qt::CaseInsensitive) != 0) {
        if (errorMessage) {
            *errorMessage = keyError.isEmpty()
                ? QString::fromUtf8(u8"更新签名使用了未授权的发布密钥。")
                : keyError;
        }
        return false;
    }

#ifdef Q_OS_WIN
    const QByteArray der = publicKeyDer(publicKeyPem, errorMessage);
    if (der.isEmpty()) return false;

    CERT_PUBLIC_KEY_INFO* publicKeyInfo = nullptr;
    DWORD decodedSize = 0;
    if (!CryptDecodeObjectEx(X509_ASN_ENCODING, X509_PUBLIC_KEY_INFO,
                             reinterpret_cast<const BYTE*>(der.constData()),
                             static_cast<DWORD>(der.size()), CRYPT_DECODE_ALLOC_FLAG,
                             nullptr, &publicKeyInfo, &decodedSize)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("CryptDecodeObjectEx error 0x%1")
                .arg(static_cast<qulonglong>(GetLastError()), 8, 16, QLatin1Char('0'));
        }
        return false;
    }

    BCRYPT_KEY_HANDLE keyHandle = nullptr;
    const BOOL imported = CryptImportPublicKeyInfoEx2(
        X509_ASN_ENCODING, publicKeyInfo, 0, nullptr, &keyHandle);
    LocalFree(publicKeyInfo);
    if (!imported || !keyHandle) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("CryptImportPublicKeyInfoEx2 error 0x%1")
                .arg(static_cast<qulonglong>(GetLastError()), 8, 16, QLatin1Char('0'));
        }
        return false;
    }

    const QByteArray digest = QCryptographicHash::hash(
        buildReleaseSignaturePayload(data), QCryptographicHash::Sha256);
    BCRYPT_PKCS1_PADDING_INFO paddingInfo{BCRYPT_SHA256_ALGORITHM};
    const NTSTATUS status = BCryptVerifySignature(
        keyHandle, &paddingInfo,
        reinterpret_cast<PUCHAR>(const_cast<char*>(digest.constData())),
        static_cast<ULONG>(digest.size()),
        reinterpret_cast<PUCHAR>(const_cast<char*>(data.signature.constData())),
        static_cast<ULONG>(data.signature.size()), BCRYPT_PAD_PKCS1);
    BCryptDestroyKey(keyHandle);
    if (status != 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("BCryptVerifySignature status 0x%1")
                .arg(static_cast<qulonglong>(static_cast<unsigned long>(status)),
                     8, 16, QLatin1Char('0'));
        }
        return false;
    }
    return true;
#else
    Q_UNUSED(publicKeyPem);
    if (errorMessage) *errorMessage = QString::fromUtf8(u8"发布签名验证仅支持 Windows。");
    return false;
#endif
}

} // namespace fpdz
