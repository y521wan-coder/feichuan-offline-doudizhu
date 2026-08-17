#include "signature_verifier.h"

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <softpub.h>
#include <wintrust.h>
#endif

namespace fpdz {

bool verifyDeveloperSignature(const QString& filePath, QString* errorMessage) {
#ifdef Q_OS_WIN
    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct = sizeof(fileInfo);
    const std::wstring nativePath = filePath.toStdWString();
    fileInfo.pcwszFilePath = nativePath.c_str();

    WINTRUST_DATA trustData{};
    trustData.cbStruct = sizeof(trustData);
    trustData.dwUIChoice = WTD_UI_NONE;
    trustData.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
    trustData.dwUnionChoice = WTD_CHOICE_FILE;
    trustData.pFile = &fileInfo;
    trustData.dwStateAction = WTD_STATEACTION_VERIFY;
    trustData.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;

    GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG result = WinVerifyTrust(nullptr, &policy, &trustData);
    trustData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &policy, &trustData);
    if (result == ERROR_SUCCESS) return true;
    if (errorMessage) {
        *errorMessage = QStringLiteral("WinVerifyTrust error 0x%1")
            .arg(static_cast<qulonglong>(static_cast<unsigned long>(result)), 8, 16,
                 QLatin1Char('0'));
    }
    return false;
#else
    Q_UNUSED(filePath);
    if (errorMessage) *errorMessage = QStringLiteral("仅支持 Windows Authenticode 验证");
    return false;
#endif
}

} // namespace fpdz
