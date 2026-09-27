#pragma once

#include <QString>

namespace fpdz {

class OnlineStorage {
public:
    static QString dataDir();
    static QString readSession();
    static bool writeSession(const QString& token);
    static void clearSession();
    static QString emojiSoundPath(int id);
    static bool importEmojiSound(int id, const QString& source, QString* error);
    static bool removeEmojiSound(int id);
};

} // namespace fpdz
