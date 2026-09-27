#pragma once

#include "accessibility/accessibility_service.h"
#include "ui/sound_service.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>
#include <QWidget>

class QCheckBox;
class QDialog;
class QLineEdit;
class QListWidget;
class QLabel;
class QPushButton;
class QSpinBox;
class QStackedWidget;

namespace fpdz {

class OnlineWindow final : public QWidget {
public:
    explicit OnlineWindow(QWidget* parent = nullptr);
    ~OnlineWindow() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    QWidget* buildLoginPage();
    QWidget* buildCountPage();
    QWidget* buildLobbyPage();
    QWidget* buildRoomPage();
    void connectServer();
    void onConnected();
    void onDisconnected();
    void receive(const QString& raw);
    void handleOk(const QString& originalType, const QJsonObject& payload);
    void handleError(const QString& code, const QJsonObject& payload);
    QString send(const QString& type, const QJsonObject& payload = {}, bool roomScoped = false);
    void showLogin();
    void showCount();
    void showLobby();
    void showRoom();
    void refreshLobby(const QJsonArray& rooms);
    void refreshRoom(const QJsonObject& view);
    void refreshHand(const QJsonArray& ids);
    void navigateRoom(int direction);
    void joinSelectedRoom(const QString& invitationId = {});
    void leaveRoom();
    void startRound();
    void bid();
    void playSelected();
    void pass();
    void announce(const QString& text, AnnouncementCategory category = AnnouncementCategory::System,
                  AnnouncementPriority priority = AnnouncementPriority::Normal);
    void showEmojiDialog();
    void showChatDialog();
    void showSettingsDialog();
    void showTargetInviteDialog(const QJsonArray& players);
    void copyDiagnostics();
    void playEmojiSound(int id);
    QString roomDescription(const QJsonObject& summary) const;
    QString errorText(const QString& code) const;
    QString visibleCardNames(const QJsonArray& ids) const;
    bool isRoundActive() const;

    QWebSocket socket_;
    QTimer reconnectTimer_;
    QTimer roomListTimer_;
    QTimer chatAnnouncementTimer_;
    QStackedWidget* pages_ = nullptr;
    QLineEdit* usernameEdit_ = nullptr;
    QLineEdit* nicknameEdit_ = nullptr;
    QLineEdit* passwordEdit_ = nullptr;
    QCheckBox* rememberCheck_ = nullptr;
    QLabel* connectionLabel_ = nullptr;
    QLabel* announcementLabel_ = nullptr;
    QLabel* lobbyTitle_ = nullptr;
    QListWidget* lobbyList_ = nullptr;
    QLabel* roomStatus_ = nullptr;
    QListWidget* seatList_ = nullptr;
    QListWidget* handList_ = nullptr;
    QLabel* previewLabel_ = nullptr;
    QSpinBox* bidSpin_ = nullptr;
    QPointer<QDialog> chatDialog_;
    QListWidget* chatMessages_ = nullptr;
    QLineEdit* chatInput_ = nullptr;
    QString username_;
    QString token_;
    QString roomId_;
    QString roundId_;
    QString seq_;
    QString lastError_;
    QString lastShownResultRound_;
    int playerCount_ = 3;
    int selfSeat_ = -1;
    bool closing_ = false;
    bool remember_ = false;
    bool recovering_ = false;
    int resumeAttempts_ = 0;
    bool acceptsInvites_ = true;
    QJsonObject roomView_;
    QJsonArray roomSummaries_;
    QJsonArray recentMessages_;
    QStringList queuedChatAnnouncements_;
    QMap<QString, QString> pending_;
    QSet<int> selectedCards_;
    AccessibilityService accessibility_;
    SoundService sound_;
};

} // namespace fpdz
