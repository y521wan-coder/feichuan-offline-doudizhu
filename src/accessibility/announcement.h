#pragma once
#include <string>
#include <cstdint>
namespace fpdz {
enum class AnnouncementCategory : uint8_t {
    Focus, CardNavigation, CardSelection, Turn, Bid, Play, Pass,
    Error, Multiplier, LowCards, Result, System
};
enum class AnnouncementPriority : uint8_t { Critical, High, Normal, Low };
enum class AnnouncementPrivacy : uint8_t { Public, HumanPrivate, DebugOnly };
struct Announcement {
    uint64_t id = 0;
    std::wstring text;
    AnnouncementCategory category = AnnouncementCategory::System;
    AnnouncementPriority priority = AnnouncementPriority::Normal;
    bool interrupt = false;
    bool replaceSameCategory = false;
    int64_t expiresAtMs = 0;
    AnnouncementPrivacy privacy = AnnouncementPrivacy::Public;
};
} // namespace fpdz
