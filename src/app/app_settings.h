#pragma once

#include "../ai/ai_difficulty.h"
#include "shortcut_settings.h"
#include "../core/audio/sound_category.h"
#include "../core/model/player.h"
#include <array>
#include <QJsonObject>
#include <QString>

namespace fpdz {

enum class PlayerVoice {
    Male = 0,
    Female = 1
};

enum class BackgroundMusicMode {
    Automatic = 0,
    Background = 1,
    Normal = 2,
    Intense = 3
};

struct AppSettings {
    int playerCount = PLAYER_COUNT; // 2/3=single deck, 4=double deck
    int aiDifficulty = static_cast<int>(AiDifficulty::Beginner);
    int aiDelay = 2; // 0=none, 1=short, 2=normal, 3=long
    bool autoNextRound = false;
    bool confirmBeforePlay = true;
    bool autoHintOnInvalid = true;
    int sortMode = 0; // 0=by rank, 1=by pattern, 2=keep order
    bool soundEnabled = true;
    int soundVolume = 80;
    SoundCategorySettings soundCategories = [] {
        SoundCategorySettings values{};
        values.fill(true);
        values[soundCategoryIndex(SoundCategory::BackgroundMusic)] = false;
        return values;
    }();
    PlayerVoice humanVoice = PlayerVoice::Male;
    bool backgroundMusicEnabled = false;
    int backgroundMusicVolume = 35;
    BackgroundMusicMode backgroundMusicMode = BackgroundMusicMode::Automatic;
    bool animationEnabled = false;
    bool autoPassEnabled = true;
    int autoPassSeconds = 30;
    bool landlordMustLeadFirstTurn = true;
    bool automaticUpdateChecks = true;
    QString lastAutomaticUpdateCheckDate;
    QString ignoredUpdateVersion;
    bool firstRunGuideShown = false;
    int firstRunGuideRevision = 0;
    std::array<QString, PLAYER_COUNT> playerNames{};
    ShortcutSettings shortcuts;

    void normalize();
    bool isSoundCategoryEnabled(SoundCategory category) const;
    void setSoundCategoryEnabled(SoundCategory category, bool enabled);
    QJsonObject toJson() const;
    static AppSettings fromJson(const QJsonObject& json);
};

} // namespace fpdz
