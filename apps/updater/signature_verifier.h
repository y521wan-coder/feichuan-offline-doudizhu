#pragma once

#include <QByteArray>
#include <QString>

namespace fpdz {

struct ReleaseSignatureData {
    QString productKey;
    QString platform;
    QString channel;
    QString version;
    qint64 fileSize = 0;
    QByteArray sha256;
    QString algorithm;
    int payloadVersion = 0;
    QString signingKeyId;
    QByteArray signature;
};

QByteArray buildReleaseSignaturePayload(const ReleaseSignatureData& data);
QString releaseSigningKeyId(const QByteArray& publicKeyPem, QString* errorMessage = nullptr);
bool verifyReleaseSignature(const ReleaseSignatureData& data,
                            const QByteArray& publicKeyPem,
                            QString* errorMessage = nullptr);

} // namespace fpdz
