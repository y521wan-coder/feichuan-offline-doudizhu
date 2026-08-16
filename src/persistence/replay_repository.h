#pragma once
#include <QString>
namespace fpdz {
class ReplayRepository {
public:
    bool saveReplay(const QString& path, const QString& data);
    bool loadReplay(const QString& path, QString& data);
};
} // namespace fpdz
