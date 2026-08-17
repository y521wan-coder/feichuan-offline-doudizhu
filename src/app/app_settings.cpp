#include "app_settings.h"

#include <algorithm>
#include <QJsonArray>

namespace fpdz {

namespace {

int jsonInt(const QJsonObject& json, const char* key, int fallback) {
    const auto value = json.value(QString::fromUtf8(key));
    return value.isDouble() ? value.toInt() : fallback;
}

bool jsonBool(const QJsonObject& json, const char* key, bool fallback) {
    const auto value = json.value(QString::fromUtf8(key));
    return value.isBool() ? value.toBool() : fallback;
}

QString jsonString(const QJsonObject& json, const char* key, const QString& fallback = {}) {
    const auto value = json.value(QString::fromUtf8(key));
    return value.isString() ? value.toString() : fallback;
}

QString defaultPlayerName(int index) {
    return QString::fromStdWString(playerIdDisplayName(static_cast<PlayerId>(index)));
}

const char* soundCategoryKey(SoundCategory category) {
    switch (category) {
    case SoundCategory::StartupDeal: return "startupDeal";
    case SoundCategory::BiddingLandlord: return "biddingLandlord";
    case SoundCategory::CardPattern: return "cardPattern";
    case SoundCategory::Pass: return "pass";
    case SoundCategory::LowCards: return "lowCards";
    case SoundCategory::Multiplier: return "multiplier";
    case SoundCategory::YourTurn: return "yourTurn";
    case SoundCategory::CardSelection: return "cardSelection";
    case SoundCategory::InvalidAction: return "invalidAction";
    case SoundCategory::GameResult: return "gameResult";
    case SoundCategory::BackgroundMusic: return "backgroundMusic";
    case SoundCategory::Count: break;
    }
    return "unknown";
}

} // namespace

void AppSettings::normalize() {
    aiDifficulty = std::clamp(aiDifficulty, 0, 2);
    aiDelay = std::clamp(aiDelay, 0, 3);
    sortMode = std::clamp(sortMode, 0, 2);
    soundVolume = std::clamp(soundVolume, 0, 100);
    humanVoice = static_cast<PlayerVoice>(
        std::clamp(static_cast<int>(humanVoice), 0, 1));
    backgroundMusicVolume = std::clamp(backgroundMusicVolume, 0, 100);
    backgroundMusicMode = static_cast<BackgroundMusicMode>(
        std::clamp(static_cast<int>(backgroundMusicMode), 0, 3));
    soundCategories[soundCategoryIndex(SoundCategory::BackgroundMusic)] =
        backgroundMusicEnabled;
    autoPassSeconds = std::clamp(autoPassSeconds, 3, 1800);
    firstRunGuideRevision = std::max(0, firstRunGuideRevision);
    for (int i = 0; i < PLAYER_COUNT; ++i) {
        playerNames[static_cast<size_t>(i)] = playerNames[static_cast<size_t>(i)].trimmed();
        if (playerNames[static_cast<size_t>(i)].isEmpty()) {
            playerNames[static_cast<size_t>(i)] = defaultPlayerName(i);
        }
    }
}

bool AppSettings::isSoundCategoryEnabled(SoundCategory category) const {
    if (category == SoundCategory::Count) return false;
    return soundCategories[soundCategoryIndex(category)];
}

void AppSettings::setSoundCategoryEnabled(SoundCategory category, bool enabled) {
    if (category == SoundCategory::Count) return;
    soundCategories[soundCategoryIndex(category)] = enabled;
    if (category == SoundCategory::BackgroundMusic) {
        backgroundMusicEnabled = enabled;
    }
}

QJsonObject AppSettings::toJson() const {
    AppSettings normalized = *this;
    normalized.normalize();

    QJsonObject json;
    json["aiDifficulty"] = normalized.aiDifficulty;
    json["aiDelay"] = normalized.aiDelay;
    json["autoNextRound"] = normalized.autoNextRound;
    json["confirmBeforePlay"] = normalized.confirmBeforePlay;
    json["autoHintOnInvalid"] = normalized.autoHintOnInvalid;
    json["sortMode"] = normalized.sortMode;
    json["soundEnabled"] = normalized.soundEnabled;
    json["soundVolume"] = normalized.soundVolume;
    QJsonObject soundCategories;
    for (std::size_t i = 0; i < SOUND_CATEGORY_COUNT; ++i) {
        const auto category = static_cast<SoundCategory>(i);
        soundCategories[QString::fromUtf8(soundCategoryKey(category))] =
            normalized.soundCategories[i];
    }
    json["soundCategories"] = soundCategories;
    json["humanVoice"] = static_cast<int>(normalized.humanVoice);
    json["backgroundMusicEnabled"] = normalized.backgroundMusicEnabled;
    json["backgroundMusicVolume"] = normalized.backgroundMusicVolume;
    json["backgroundMusicMode"] = static_cast<int>(normalized.backgroundMusicMode);
    json["animationEnabled"] = normalized.animationEnabled;
    json["autoPassEnabled"] = normalized.autoPassEnabled;
    json["autoPassSeconds"] = normalized.autoPassSeconds;
    json["automaticUpdateChecks"] = normalized.automaticUpdateChecks;
    json["lastAutomaticUpdateCheckDate"] = normalized.lastAutomaticUpdateCheckDate;
    json["ignoredUpdateVersion"] = normalized.ignoredUpdateVersion;
    json["firstRunGuideShown"] = normalized.firstRunGuideShown;
    json["firstRunGuideRevision"] = normalized.firstRunGuideRevision;
    QJsonArray playerNames;
    for (const auto& name : normalized.playerNames) {
        playerNames.append(name);
    }
    json["playerNames"] = playerNames;
    return json;
}

AppSettings AppSettings::fromJson(const QJsonObject& json) {
    AppSettings settings;
    settings.aiDifficulty = jsonInt(json, "aiDifficulty", settings.aiDifficulty);
    settings.aiDelay = jsonInt(json, "aiDelay", settings.aiDelay);
    settings.autoNextRound = jsonBool(json, "autoNextRound", settings.autoNextRound);
    settings.confirmBeforePlay = jsonBool(json, "confirmBeforePlay", settings.confirmBeforePlay);
    settings.autoHintOnInvalid = jsonBool(json, "autoHintOnInvalid", settings.autoHintOnInvalid);
    settings.sortMode = jsonInt(json, "sortMode", settings.sortMode);
    settings.soundEnabled = jsonBool(json, "soundEnabled", settings.soundEnabled);
    settings.soundVolume = jsonInt(json, "soundVolume", settings.soundVolume);
    settings.humanVoice = static_cast<PlayerVoice>(
        jsonInt(json, "humanVoice", static_cast<int>(settings.humanVoice)));
    settings.backgroundMusicEnabled = jsonBool(
        json, "backgroundMusicEnabled", settings.backgroundMusicEnabled);
    const auto soundCategoriesValue = json.value(QStringLiteral("soundCategories"));
    if (soundCategoriesValue.isObject()) {
        const auto soundCategories = soundCategoriesValue.toObject();
        for (std::size_t i = 0; i < SOUND_CATEGORY_COUNT; ++i) {
            const auto category = static_cast<SoundCategory>(i);
            settings.soundCategories[i] = jsonBool(
                soundCategories, soundCategoryKey(category), settings.soundCategories[i]);
        }
        settings.backgroundMusicEnabled =
            settings.soundCategories[soundCategoryIndex(SoundCategory::BackgroundMusic)];
    } else {
        settings.soundCategories[soundCategoryIndex(SoundCategory::BackgroundMusic)] =
            settings.backgroundMusicEnabled;
    }
    settings.backgroundMusicVolume = jsonInt(
        json, "backgroundMusicVolume", settings.backgroundMusicVolume);
    settings.backgroundMusicMode = static_cast<BackgroundMusicMode>(
        jsonInt(json, "backgroundMusicMode", static_cast<int>(settings.backgroundMusicMode)));
    settings.animationEnabled = jsonBool(json, "animationEnabled", settings.animationEnabled);
    settings.autoPassEnabled = jsonBool(json, "autoPassEnabled", settings.autoPassEnabled);
    settings.autoPassSeconds = jsonInt(json, "autoPassSeconds", settings.autoPassSeconds);
    settings.automaticUpdateChecks = jsonBool(
        json, "automaticUpdateChecks", settings.automaticUpdateChecks);
    settings.lastAutomaticUpdateCheckDate = jsonString(json, "lastAutomaticUpdateCheckDate");
    settings.ignoredUpdateVersion = jsonString(json, "ignoredUpdateVersion");
    settings.firstRunGuideShown = jsonBool(
        json, "firstRunGuideShown", settings.firstRunGuideShown);
    settings.firstRunGuideRevision = jsonInt(
        json, "firstRunGuideRevision", settings.firstRunGuideRevision);
    const auto playerNames = json.value("playerNames").toArray();
    for (int i = 0; i < playerNames.size() && i < PLAYER_COUNT; ++i) {
        settings.playerNames[static_cast<size_t>(i)] = playerNames[i].toString();
    }
    settings.normalize();
    return settings;
}

} // namespace fpdz
