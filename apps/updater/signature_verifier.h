#pragma once

#include <QString>

namespace fpdz {

bool verifyDeveloperSignature(const QString& filePath, QString* errorMessage = nullptr);

} // namespace fpdz
