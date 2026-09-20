#include "accessibility_service.h"
#include <QAccessible>
#include <QAccessibleWidget>
#include <QObject>
#include <QWidget>
#include <mutex>

namespace fpdz {

namespace {

const auto kGameStatusAnnouncementObjectName = QStringLiteral("gameStatusAnnouncement");

class GameStatusAccessible final : public QAccessibleWidget {
public:
    explicit GameStatusAccessible(QWidget* widget)
        // Use the same ordinary role handled by screen readers in Alt menus.
        // The action interface remains disabled below, so this is announcement-only.
        : QAccessibleWidget(widget, QAccessible::MenuItem) {
    }

    QString text(QAccessible::Text textType) const override {
        if (textType == QAccessible::Name) {
            return QAccessibleWidget::text(textType);
        }
        return {};
    }

    void* interface_cast(QAccessible::InterfaceType) override {
        return nullptr;
    }
};

QAccessibleInterface* gameStatusAccessibleFactory(const QString&, QObject* object) {
    auto* widget = qobject_cast<QWidget*>(object);
    if (!widget || widget->objectName() != kGameStatusAnnouncementObjectName) {
        return nullptr;
    }
    return new GameStatusAccessible(widget);
}

void installGameStatusAccessibleFactory() {
    static std::once_flag once;
    std::call_once(once, []() {
        QAccessible::installFactory(gameStatusAccessibleFactory);
    });
}

} // namespace

AccessibilityService::AccessibilityService() {
    installGameStatusAccessibleFactory();
}

bool AccessibilityService::announce(const Announcement& announcement, QObject* target) {
    if (!target || announcement.text.empty()) return false;
    auto scheduled = m_scheduler.schedule(announcement);
    if (!scheduled) return false;
    if (scheduled->category != AnnouncementCategory::CardSelection &&
        m_lastAnnouncementTimer.isValid() && m_lastAnnouncementTimer.elapsed() < 150 &&
        scheduled->text == m_lastAnnouncementText &&
        scheduled->category == m_lastAnnouncementCategory) {
        return false;
    }
    auto* widget = qobject_cast<QWidget*>(target);
    if (!widget) return false;

    // Keep the message represented in the standard accessibility tree. Dynamic
    // game speech uses the official API of whichever supported reader is active;
    // the traditional Qt/UIA event remains the no-reader/no-API fallback.
    widget->setAccessibleName(QString::fromStdWString(scheduled->text));
    const ScreenReaderDelivery delivery = m_screenReaderBridge.speak(scheduled->text, true);
    if (delivery != ScreenReaderDelivery::PrivateApi) {
        // A reader started after the game can inherit a stale Windows/Qt
        // accessibility session from the reader that just exited. Refresh the
        // standard backend once at that process-session boundary, then keep the
        // existing focus-event fallback for readers without a usable private API.
        if (m_screenReaderBridge.readerSessionChanged()) {
            QAccessible::setActive(false);
            QAccessible::setActive(true);
        }
        if (delivery == ScreenReaderDelivery::StandardAnnouncement) {
            QAccessibleAnnouncementEvent event(
                widget, QString::fromStdWString(scheduled->text));
            if (scheduled->priority == AnnouncementPriority::Critical ||
                scheduled->priority == AnnouncementPriority::High) {
                event.setPoliteness(QAccessible::AnnouncementPoliteness::Assertive);
            }
            QAccessible::updateAccessibility(&event);
        } else {
            QAccessibleEvent event(widget, QAccessible::Focus);
            QAccessible::updateAccessibility(&event);
        }
    }
    m_lastAnnouncementText = scheduled->text;
    m_lastAnnouncementCategory = scheduled->category;
    m_lastAnnouncementTimer.restart();
    return true;
}
void AccessibilityService::stopCurrent() {
    m_screenReaderBridge.stop();
}
void AccessibilityService::clearQueue() {
    m_scheduler.clear();
}
std::wstring AccessibilityService::routeName() const { return m_screenReaderBridge.routeName(); }
std::wstring AccessibilityService::backendStatus() const {
    const std::wstring reader = m_screenReaderBridge.backendName();
    const std::wstring qtStatus = QAccessible::isActive() ? L"Qt无障碍已激活" :
                                                           L"Qt无障碍接口就绪，当前未激活";
    return reader + L"；" + qtStatus;
}
std::wstring AccessibilityService::backendName() const {
    return m_screenReaderBridge.backendName();
}
} // namespace fpdz
