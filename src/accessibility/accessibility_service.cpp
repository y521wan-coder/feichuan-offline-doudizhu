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

    // Some Windows screen readers can navigate Qt/UIA controls but do not handle
    // UIA notification events. Expose the message as ordinary control text and
    // publish a traditional accessibility focus event without moving Qt's real
    // keyboard focus.
    widget->setAccessibleName(QString::fromStdWString(scheduled->text));
    QAccessibleEvent event(widget, QAccessible::Focus);
    QAccessible::updateAccessibility(&event);
    m_lastAnnouncementText = scheduled->text;
    m_lastAnnouncementCategory = scheduled->category;
    m_lastAnnouncementTimer.restart();
    return true;
}
void AccessibilityService::stopCurrent() {
}
void AccessibilityService::clearQueue() {
    m_scheduler.clear();
}
std::wstring AccessibilityService::routeName() const { return L"Qt/UIA 标准模式"; }
std::wstring AccessibilityService::backendStatus() const {
    return QAccessible::isActive() ? L"Qt无障碍已激活" : L"Qt无障碍接口就绪，当前未激活";
}
std::wstring AccessibilityService::backendName() const {
    return L"具体读屏软件未知";
}
} // namespace fpdz
