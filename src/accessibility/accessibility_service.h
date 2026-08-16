#pragma once
#include "announcement.h"
#include "announcement_scheduler.h"
#include <QElapsedTimer>
#include <string>
class QObject;
namespace fpdz {
class AccessibilityService {
public:
    AccessibilityService();
    ~AccessibilityService() = default;
    bool announce(const Announcement& announcement, QObject* target);
    void stopCurrent();
    void clearQueue();
    std::wstring routeName() const;
    std::wstring backendStatus() const;
    std::wstring backendName() const;
private:
    AnnouncementScheduler m_scheduler;
    QElapsedTimer m_lastAnnouncementTimer;
    std::wstring m_lastAnnouncementText;
    AnnouncementCategory m_lastAnnouncementCategory = AnnouncementCategory::System;
};
} // namespace fpdz
