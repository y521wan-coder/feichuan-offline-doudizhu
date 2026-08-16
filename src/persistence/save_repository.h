#pragma once
#include <QString>
#include "../core/engine/game_state.h"

namespace fpdz {

class SaveRepository {
public:
    bool saveGame(const QString& path, const GameState& state);
    bool loadGame(const QString& path, GameState& state);
    bool hasAutoSave(const QString& path) const;
    bool deleteSave(const QString& path);
};

} // namespace fpdz
