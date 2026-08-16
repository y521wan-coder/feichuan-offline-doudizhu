#include "announcement_scheduler.h"
namespace fpdz {
std::optional<Announcement> AnnouncementScheduler::schedule(const Announcement& announcement) {
    Announcement a = announcement;
    a.id = m_nextId++;
    if (a.priority == AnnouncementPriority::Critical || a.priority == AnnouncementPriority::High) {
        return a;
    }
    if (a.replaceSameCategory && m_lastNormal && m_lastNormal->category == a.category) {
        m_lastNormal = a;
        return a;
    }
    m_lastNormal = a;
    return a;
}
void AnnouncementScheduler::clear() { m_lastNormal.reset(); }
bool AnnouncementScheduler::isEmpty() const { return !m_lastNormal.has_value(); }
} // namespace fpdz
