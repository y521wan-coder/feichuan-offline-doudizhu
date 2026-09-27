#pragma once

#include <QMetaType>

namespace fpdz {

enum class GameMode {
    Offline,
    AiBattle,
    Online
};

} // namespace fpdz

Q_DECLARE_METATYPE(fpdz::GameMode)
