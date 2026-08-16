#pragma once
#include "announcement.h"
#include <queue>
#include <optional>
namespace fpdz {
class AnnouncementScheduler {
public:
    std::optional<Announcement> schedule(const Announcement& announcement);
    void clear();
    bool isEmpty() const;
private:
    uint64_t m_nextId = 1;
    std::optional<Announcement> m_lastNormal;
};
} // namespace fpdz
