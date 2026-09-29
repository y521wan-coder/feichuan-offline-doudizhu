#include "online_window.h"
#include "online_lobby_controls.h"
#include "online_storage.h"

#include "core/model/card.h"
#include "core/engine/game_phase.h"
#include "core/rules/pattern_analyzer.h"
#include "core/rules/pattern_comparator.h"
#include "core/text/card_text_formatter.h"
#include "ai/legal_move_generator.h"
#include "ui/models/hand_list_model.h"
#include "ui/widgets/card_table_widget.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QShortcut>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QUuid>
#include <QWebSocketProtocol>
#include <algorithm>
#include <optional>
#include <vector>
#include <windows.h>
#include <mmsystem.h>

namespace fpdz {

namespace {
const QStringList kEmojiNames = {QStringLiteral("微笑"), QStringLiteral("大笑"),
    QStringLiteral("鼓掌"), QStringLiteral("加油"), QStringLiteral("惊讶"),
    QStringLiteral("无奈"), QStringLiteral("鲜花"), QStringLiteral("鸡蛋")};

QString roomNumber(const QString& roomId) { return roomId.section(QLatin1Char('-'), 1); }

qint64 deadline(const QJsonObject& view) {
    return view.value(QStringLiteral("deadline_ms")).toString().toLongLong();
}

QString phaseText(int phase) {
    switch (phase) {
    case 2: return QStringLiteral("叫分中");
    case 4: return QStringLiteral("出牌中");
    case 7: return QStringLiteral("已结算");
    default: return QStringLiteral("准备中");
    }
}

QString statusText(const QString& status) {
    if (status == QStringLiteral("waiting")) return QStringLiteral("等待中");
    if (status == QStringLiteral("playing")) return QStringLiteral("牌局中");
    if (status == QStringLiteral("finished")) return QStringLiteral("已结算");
    return status;
}

QJsonObject visibleMessage(const QJsonObject& incoming) {
    return {{QStringLiteral("nickname"), incoming.value(QStringLiteral("nickname")).toString()},
            {QStringLiteral("text"), incoming.value(QStringLiteral("text")).toString()},
            {QStringLiteral("time_ms"), incoming.value(QStringLiteral("time_ms")).toString()}};
}

QJsonArray visibleCards(const QJsonArray& incoming) {
    QJsonArray ids;
    QSet<int> seen;
    for (const QJsonValue& value : incoming) {
        const int id = value.toInt(-1);
        if (id >= 0 && id < TOTAL_CARDS && !seen.contains(id)) {
            ids.append(id);
            seen.insert(id);
        }
    }
    return ids;
}

QJsonObject visibleView(const QJsonObject& incoming) {
    QJsonObject safe;
    const QStringList fields = {QStringLiteral("room_id"), QStringLiteral("player_count"),
        QStringLiteral("occupied"), QStringLiteral("status"), QStringLiteral("password_required"),
        QStringLiteral("seq"), QStringLiteral("round_id"), QStringLiteral("turn_seconds"),
        QStringLiteral("host"), QStringLiteral("is_host"), QStringLiteral("deadline_ms"),
        QStringLiteral("record_eligible"), QStringLiteral("self_seat"),
        QStringLiteral("phase"), QStringLiteral("current_player"),
        QStringLiteral("base_score"), QStringLiteral("multiplier"),
        QStringLiteral("bottom_revealed"), QStringLiteral("last_played_by"),
        QStringLiteral("winner")};
    for (const QString& key : fields)
        if (incoming.contains(key)) safe[key] = incoming.value(key);
    QJsonArray seats;
    for (const QJsonValue& value : incoming.value(QStringLiteral("seats")).toArray()) {
        const QJsonObject seat = value.toObject();
        QJsonObject copy;
        for (const QString& key : {QStringLiteral("index"), QStringLiteral("nickname"),
                                   QStringLiteral("short_id"), QStringLiteral("remaining"),
                                   QStringLiteral("role"), QStringLiteral("bid")})
            if (seat.contains(key)) copy[key] = seat.value(key);
        seats.append(copy);
    }
    safe[QStringLiteral("seats")] = seats;
    safe[QStringLiteral("last_played_cards")] = visibleCards(
        incoming.value(QStringLiteral("last_played_cards")).toArray());
    if (incoming.value(QStringLiteral("bottom_revealed")).toBool())
        safe[QStringLiteral("bottom_cards")] = visibleCards(
            incoming.value(QStringLiteral("bottom_cards")).toArray());
    if (incoming.value(QStringLiteral("self_seat")).toInt(-1) >= 0
        && incoming.contains(QStringLiteral("hand")))
        safe[QStringLiteral("hand")] = visibleCards(
            incoming.value(QStringLiteral("hand")).toArray());
    if (incoming.contains(QStringLiteral("score_changes")))
        safe[QStringLiteral("score_changes")] = incoming.value(QStringLiteral("score_changes")).toArray();
    return safe;
}

QPushButton* makeButton(const QString& text, QWidget* parent) {
    auto* button = new OnlineButton(text, parent);
    button->setAccessibleName(text);
    return button;
}
} // namespace

OnlineWindow::OnlineWindow(QWidget* parent, bool connectOnStart) : QWidget(parent) {
    setWindowTitle(QStringLiteral("飞船斗地主 在线真人版"));
    resize(800, 660);
    setMinimumSize(620, 500);
    if (connectOnStart) sound_.initialize();
    buildUi();
    for (auto* widget : findChildren<QWidget*>()) widget->installEventFilter(this);
    const auto roomShortcut = [this](Qt::Key key, auto action) {
        auto* shortcut = new QShortcut(QKeySequence(key), this);
        shortcut->setContext(Qt::WindowShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, action]() {
            if (pages_->currentIndex() == 3) (this->*action)();
        });
    };
    roomShortcut(Qt::Key_F1, &OnlineWindow::startRound);
    roomShortcut(Qt::Key_F3, &OnlineWindow::showEmojiDialog);
    roomShortcut(Qt::Key_F4, &OnlineWindow::showChatDialog);
    roomShortcut(Qt::Key_F5, &OnlineWindow::showSettingsDialog);
    auto* bottomShortcut = new QShortcut(QKeySequence(Qt::Key_F2), this);
    bottomShortcut->setContext(Qt::WindowShortcut);
    connect(bottomShortcut, &QShortcut::activated, this, [this]() {
        if (pages_->currentIndex() != 3) return;
        announce(roomView_.value(QStringLiteral("bottom_revealed")).toBool()
            ? QStringLiteral("已公开底牌：")
                + visibleCardNames(roomView_.value(QStringLiteral("bottom_cards")).toArray())
            : QStringLiteral("底牌尚未公开"));
    });
    connect(&socket_, &QWebSocket::connected, this, [this]() { onConnected(); });
    connect(&socket_, &QWebSocket::disconnected, this, [this]() { onDisconnected(); });
    connect(&socket_, &QWebSocket::textMessageReceived, this,
            [this](const QString& raw) { receive(raw); });
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(&socket_, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
#else
    connect(&socket_, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
            this, [this](QAbstractSocket::SocketError) {
#endif
        lastError_ = QStringLiteral("network_error");
        connectionLabel_->setText(QStringLiteral("联机服务器连接失败，离线单机和 AI 对战仍可从模式选择进入"));
        announce(QStringLiteral("联机服务器连接失败。可以返回模式选择。"),
                 AnnouncementCategory::Error, AnnouncementPriority::High);
    });
    reconnectTimer_.setInterval(3000);
    connect(&reconnectTimer_, &QTimer::timeout, this, [this]() {
        if (!closing_ && socket_.state() == QAbstractSocket::UnconnectedState) connectServer();
    });
    roomListTimer_.setInterval(5000);
    connect(&roomListTimer_, &QTimer::timeout, this, [this]() {
        if (pages_->currentIndex() == 2 && !username_.isEmpty()
            && socket_.state() == QAbstractSocket::ConnectedState)
            send(QStringLiteral("list"), {{QStringLiteral("player_count"), playerCount_}});
    });
    chatAnnouncementTimer_.setInterval(1600);
    connect(&chatAnnouncementTimer_, &QTimer::timeout, this, [this]() {
        if (queuedChatAnnouncements_.isEmpty()) { chatAnnouncementTimer_.stop(); return; }
        announce(queuedChatAnnouncements_.takeFirst());
        if (queuedChatAnnouncements_.isEmpty()) chatAnnouncementTimer_.stop();
    });
    if (connectOnStart) {
        token_ = OnlineStorage::readSession();
        remember_ = !token_.isEmpty();
        rememberCheck_->setChecked(remember_);
        connectServer();
    }
}

OnlineWindow::~OnlineWindow() {
    closing_ = true;
    socket_.close();
    sound_.shutdown();
}

void OnlineWindow::buildUi() {
    auto* root = new QVBoxLayout(this);
    connectionLabel_ = new QLabel(QStringLiteral("正在连接真人联机服务器"), this);
    connectionLabel_->setWordWrap(true);
    root->addWidget(connectionLabel_);
    pages_ = new QStackedWidget(this);
    pages_->addWidget(buildLoginPage());
    pages_->addWidget(buildCountPage());
    pages_->addWidget(buildLobbyPage());
    pages_->addWidget(buildRoomPage());
    root->addWidget(pages_, 1);
    announcementLabel_ = new QLabel(this);
    announcementLabel_->setObjectName(QStringLiteral("gameStatusAnnouncement"));
    announcementLabel_->setAccessibleName(QStringLiteral("在线状态"));
    announcementLabel_->setWordWrap(true);
    root->addWidget(announcementLabel_);
    auto* bottom = new QHBoxLayout;
    auto* diagnostics = makeButton(QStringLiteral("复制诊断信息"), this);
    auto* returnButton = makeButton(QStringLiteral("返回模式选择"), this);
    bottom->addWidget(diagnostics);
    bottom->addStretch(1);
    bottom->addWidget(returnButton);
    root->addLayout(bottom);
    connect(diagnostics, &QPushButton::clicked, this, [this]() { copyDiagnostics(); });
    connect(returnButton, &QPushButton::clicked, this, [this]() { close(); });
}

QWidget* OnlineWindow::buildLoginPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(new QLabel(QStringLiteral("在线真人版账号"), page));
    auto* form = new QFormLayout;
    usernameEdit_ = new QLineEdit(page);
    usernameEdit_->setMaxLength(24);
    usernameEdit_->setAccessibleName(QStringLiteral("用户名，3 至 24 位英文字母、数字或下划线"));
    nicknameEdit_ = new QLineEdit(page);
    nicknameEdit_->setMaxLength(16);
    nicknameEdit_->setAccessibleName(QStringLiteral("公开昵称，注册时填写"));
    passwordEdit_ = new QLineEdit(page);
    passwordEdit_->setEchoMode(QLineEdit::Password);
    passwordEdit_->setMaxLength(128);
    passwordEdit_->setAccessibleName(QStringLiteral("密码，10 至 128 个字符"));
    form->addRow(QStringLiteral("用户名"), usernameEdit_);
    form->addRow(QStringLiteral("公开昵称"), nicknameEdit_);
    form->addRow(QStringLiteral("密码"), passwordEdit_);
    layout->addLayout(form);
    rememberCheck_ = new QCheckBox(QStringLiteral("记住登录（使用 Windows 加密保存）"), page);
    layout->addWidget(rememberCheck_);
    auto* buttons = new QHBoxLayout;
    auto* login = makeButton(QStringLiteral("登录"), page);
    auto* registerButton = makeButton(QStringLiteral("注册"), page);
    buttons->addWidget(login);
    buttons->addWidget(registerButton);
    layout->addLayout(buttons);
    layout->addStretch(1);
    connect(login, &QPushButton::clicked, this, [this]() {
        if (socket_.state() != QAbstractSocket::ConnectedState) {
            announce(QStringLiteral("尚未连接服务器，请稍后重试或返回模式选择"), AnnouncementCategory::Error);
            return;
        }
        const QString username = usernameEdit_->text().trimmed();
        const QString password = passwordEdit_->text();
        if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_]{3,24}$")).match(username).hasMatch()
            || password.size() < 10 || password.size() > 128) {
            announce(QStringLiteral("请检查用户名和密码长度"), AnnouncementCategory::Error);
            return;
        }
        username_ = username;
        remember_ = rememberCheck_->isChecked();
        send(QStringLiteral("login"), {{QStringLiteral("username"), username},
                                      {QStringLiteral("password"), password}});
    });
    connect(registerButton, &QPushButton::clicked, this, [this]() {
        if (socket_.state() != QAbstractSocket::ConnectedState) {
            announce(QStringLiteral("尚未连接服务器"), AnnouncementCategory::Error); return;
        }
        const QString username = usernameEdit_->text().trimmed();
        const QString nickname = nicknameEdit_->text().trimmed();
        const QString password = passwordEdit_->text();
        if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_]{3,24}$")).match(username).hasMatch()
            || nickname.isEmpty() || nickname.size() > 16
            || password.size() < 10 || password.size() > 128) {
            announce(QStringLiteral("请检查用户名、昵称和密码长度"), AnnouncementCategory::Error);
            return;
        }
        username_ = username;
        remember_ = rememberCheck_->isChecked();
        send(QStringLiteral("register"), {{QStringLiteral("username"), username},
                                         {QStringLiteral("nickname"), nickname},
                                         {QStringLiteral("password"), password}});
    });
    connect(passwordEdit_, &QLineEdit::returnPressed, login, &QPushButton::click);
    return page;
}

QWidget* OnlineWindow::buildCountPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(new QLabel(QStringLiteral("选择真人版人数。Tab 或 Shift+Tab 移动，回车进入大厅。"), page));
    for (int count : {2, 3, 4}) {
        auto* button = makeButton(QStringLiteral("%1 人房大厅").arg(count), page);
        layout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, count]() {
            playerCount_ = count;
            showLobby();
        });
    }
    auto* logout = makeButton(QStringLiteral("退出登录"), page);
    layout->addWidget(logout);
    connect(logout, &QPushButton::clicked, this, [this]() {
        send(QStringLiteral("logout"));
        token_.clear();
        username_.clear();
        OnlineStorage::clearSession();
        showLogin();
    });
    layout->addStretch(1);
    return page;
}

QWidget* OnlineWindow::buildLobbyPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    lobbyTitle_ = new QLabel(page);
    layout->addWidget(lobbyTitle_);
    layout->addWidget(new QLabel(QStringLiteral("左右方向键循环浏览房间，回车加入。"), page));
    lobbyList_ = new QListWidget(page);
    lobbyList_->setAccessibleName(QStringLiteral("固定编号房间列表"));
    lobbyList_->installEventFilter(this);
    layout->addWidget(lobbyList_, 1);
    auto* buttons = new QHBoxLayout;
    auto* join = makeButton(QStringLiteral("加入房间"), page);
    auto* back = makeButton(QStringLiteral("返回人数选择"), page);
    buttons->addWidget(join);
    buttons->addWidget(back);
    layout->addLayout(buttons);
    connect(join, &QPushButton::clicked, this, [this]() { joinSelectedRoom(); });
    connect(back, &QPushButton::clicked, this, [this]() { roomListTimer_.stop(); showCount(); });
    connect(lobbyList_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { joinSelectedRoom(); });
    return page;
}

QWidget* OnlineWindow::buildRoomPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    roomStatus_ = new QLabel(page);
    roomStatus_->setWordWrap(true);
    layout->addWidget(roomStatus_);
    seatList_ = new QListWidget(page);
    seatList_->setAccessibleName(QStringLiteral("房间席位和剩余牌数"));
    seatList_->setMaximumHeight(110);
    layout->addWidget(seatList_);
    auto* keys = new QLabel(QStringLiteral("F1 开局，F2 已公开底牌，F3 表情，F4 房内消息，F5 在线设置"), page);
    keys->setWordWrap(true);
    layout->addWidget(keys);
    cardTable_ = new CardTableWidget(page);
    layout->addWidget(cardTable_, 1);
    handModel_ = std::make_unique<HandListModel>();
    handView_ = new QListView(page);
    handView_->setAccessibleName(QStringLiteral("手牌"));
    handView_->setFocusPolicy(Qt::StrongFocus);
    handView_->setModel(handModel_.get());
    handView_->setSelectionMode(QAbstractItemView::NoSelection);
    handView_->setFlow(QListView::LeftToRight);
    handView_->setWrapping(true);
    handView_->setMinimumHeight(120);
    handView_->setSpacing(5);
    layout->addWidget(handView_);
    connect(handModel_.get(), &QAbstractItemModel::dataChanged, this,
            [this]() { refreshSelection(); });
    previewLabel_ = new QLabel(QStringLiteral("尚未选牌"), page);
    previewLabel_->setWordWrap(true);
    layout->addWidget(previewLabel_);
    auto* actions = new QHBoxLayout;
    bidButton_ = makeButton(QStringLiteral("叫分选择"), page);
    playButton_ = makeButton(QStringLiteral("出牌(&E)"), page);
    passButton_ = makeButton(QStringLiteral("过牌(&P)"), page);
    hintButton_ = makeButton(QStringLiteral("提示(&H)"), page);
    playButton_->setAccessibleName(QStringLiteral("出牌"));
    passButton_->setAccessibleName(QStringLiteral("过牌"));
    hintButton_->setAccessibleName(QStringLiteral("出牌提示"));
    auto* leaveButton = makeButton(QStringLiteral("离开房间"), page);
    actions->addWidget(bidButton_);
    actions->addWidget(playButton_);
    actions->addWidget(passButton_);
    actions->addWidget(hintButton_);
    actions->addWidget(leaveButton);
    layout->addLayout(actions);
    playButton_->setFocusPolicy(Qt::NoFocus);
    passButton_->setFocusPolicy(Qt::NoFocus);
    hintButton_->setFocusPolicy(Qt::NoFocus);
    connect(bidButton_, &QPushButton::clicked, this, [this]() { bid(); });
    connect(playButton_, &QPushButton::clicked, this, [this]() { playSelected(); });
    connect(passButton_, &QPushButton::clicked, this, [this]() { pass(); });
    connect(hintButton_, &QPushButton::clicked, this, [this]() { showHint(); });
    connect(leaveButton, &QPushButton::clicked, this, [this]() { leaveRoom(); });
    return page;
}

void OnlineWindow::connectServer() {
    if (socket_.state() != QAbstractSocket::UnconnectedState) return;
    connectionLabel_->setText(QStringLiteral("正在连接 wss://play.327802521.xyz/ws"));
    socket_.open(QUrl(QStringLiteral("wss://play.327802521.xyz/ws")));
}

void OnlineWindow::onConnected() {
    reconnectTimer_.stop();
    connectionLabel_->setText(QStringLiteral("已连接真人联机服务器"));
    announce(QStringLiteral("服务器已连接"));
    if (!token_.isEmpty()) {
        recovering_ = true;
        ++resumeAttempts_;
        send(QStringLiteral("resume"), {{QStringLiteral("token"), token_}});
    } else if (pages_->currentIndex() != 0 && !username_.isEmpty()) {
        showLogin();
    }
}

void OnlineWindow::onDisconnected() {
    if (closing_) return;
    if (bidDialog_) bidDialog_->reject();
    connectionLabel_->setText(QStringLiteral("连接中断，正在尝试重连；也可返回模式选择"));
    announce(QStringLiteral("联机连接中断，正在重连"), AnnouncementCategory::Error,
             AnnouncementPriority::High);
    reconnectTimer_.start();
}

QString OnlineWindow::send(const QString& type, const QJsonObject& payload, bool roomScoped) {
    if (socket_.state() != QAbstractSocket::ConnectedState) {
        announce(QStringLiteral("服务器未连接"), AnnouncementCategory::Error); return {};
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject message{{QStringLiteral("version"), 1}, {QStringLiteral("type"), type},
                        {QStringLiteral("request_id"), id},
                        {QStringLiteral("room_id"), roomScoped ? QJsonValue(roomId_)
                                                       : QJsonValue(QJsonValue::Null)},
                        {QStringLiteral("round_id"), roomScoped && !roundId_.isEmpty()
                             ? QJsonValue(roundId_) : QJsonValue(QJsonValue::Null)},
                        {QStringLiteral("seq"), roomScoped && !seq_.isEmpty()
                             ? QJsonValue(seq_) : QJsonValue(QJsonValue::Null)},
                        {QStringLiteral("payload"), payload}};
    pending_[id] = type;
    socket_.sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
    return id;
}

void OnlineWindow::receive(const QString& raw) {
    if (raw.toUtf8().size() > 64 * 1024) { socket_.close(); return; }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(raw.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) return;
    const QJsonObject message = document.object();
    const int version = message.value(QStringLiteral("version")).toInt();
    if (version > 1) {
        lastError_ = QStringLiteral("protocol_version");
        announce(QStringLiteral("服务器需要更新版本，请使用原有独立更新器升级"),
                 AnnouncementCategory::Error, AnnouncementPriority::Critical);
        return;
    }
    const QString type = message.value(QStringLiteral("type")).toString();
    const QString requestId = message.value(QStringLiteral("request_id")).toString();
    const QString originalType = pending_.take(requestId);
    const QJsonObject payload = message.value(QStringLiteral("payload")).toObject();
    if (message.value(QStringLiteral("room_id")).toString() == roomId_) {
        roundId_ = message.value(QStringLiteral("round_id")).toString(roundId_);
        seq_ = message.value(QStringLiteral("seq")).toString(seq_);
    }
    if (type == QStringLiteral("error")) {
        handleError(payload.value(QStringLiteral("code")).toString(), payload);
        return;
    }
    if (type == QStringLiteral("ok")) { handleOk(originalType, payload); return; }
    if (type == QStringLiteral("view")) {
        if (message.value(QStringLiteral("room_id")).toString() == roomId_) {
            roundId_ = message.value(QStringLiteral("round_id")).toString(roundId_);
            seq_ = message.value(QStringLiteral("seq")).toString(seq_);
        }
        refreshRoom(payload);
        return;
    }
    if (type == QStringLiteral("invite")) {
        const QString inviteId = payload.value(QStringLiteral("invitation_id")).toString();
        const QString invitedRoom = payload.value(QStringLiteral("room_id")).toString();
        const QString sender = payload.value(QStringLiteral("from_nickname")).toString();
        if (pages_->currentIndex() != 2 || inviteId.isEmpty() || invitedRoom.isEmpty()) return;
        announce(QStringLiteral("%1 邀请你进入 %2 房间").arg(sender, invitedRoom));
        if (QMessageBox::question(this, QStringLiteral("房间邀请"),
                QStringLiteral("%1 邀请你进入 %2。邀请不预留席位，是否接受？")
                    .arg(sender, invitedRoom)) == QMessageBox::Yes) {
            roomId_ = invitedRoom;
            playerCount_ = invitedRoom.section(QLatin1Char('-'), 0, 0).toInt();
            seq_ = payload.value(QStringLiteral("seq")).toString();
            send(QStringLiteral("join"), {{QStringLiteral("invitation_id"), inviteId}}, true);
        }
        return;
    }
    if (type == QStringLiteral("chat")) {
        if (message.value(QStringLiteral("room_id")).toString() != roomId_) return;
        const QString nickname = payload.value(QStringLiteral("nickname")).toString();
        const QString body = payload.value(QStringLiteral("text")).toString();
        recentMessages_.append(visibleMessage(payload));
        while (recentMessages_.size() > 20) recentMessages_.removeAt(0);
        if (chatMessages_) chatMessages_->addItem(nickname + QStringLiteral("：") + body);
        queuedChatAnnouncements_.append(nickname + QStringLiteral("说：") + body);
        if (!chatAnnouncementTimer_.isActive()) chatAnnouncementTimer_.start();
        return;
    }
    if (type == QStringLiteral("emote")) {
        const int id = payload.value(QStringLiteral("id")).toInt();
        const QString nickname = payload.value(QStringLiteral("nickname")).toString();
        if (id >= 1 && id <= kEmojiNames.size()) {
            announce(QStringLiteral("%1 发送表情：%2").arg(nickname, kEmojiNames[id - 1]));
            playEmojiSound(id);
        }
    }
}

void OnlineWindow::handleOk(const QString& originalType, const QJsonObject& payload) {
    if (originalType == QStringLiteral("register") || originalType == QStringLiteral("login")
        || originalType == QStringLiteral("resume")) {
        const QString receivedToken = payload.value(QStringLiteral("token")).toString();
        if (!receivedToken.isEmpty()) token_ = receivedToken;
        if (remember_ && !token_.isEmpty()) OnlineStorage::writeSession(token_);
        else OnlineStorage::clearSession();
        username_ = payload.value(QStringLiteral("username")).toString(username_);
        acceptsInvites_ = payload.value(QStringLiteral("accept_invites")).toBool(acceptsInvites_);
        passwordEdit_->clear();
        recovering_ = false;
        resumeAttempts_ = 0;
        const QJsonObject view = payload.value(QStringLiteral("view")).toObject();
        if (!view.isEmpty()) {
            refreshRoom(view);
            showRoom();
        } else {
            showCount();
        }
        announce(QStringLiteral("已登录真人联机"));
        return;
    }
    if (originalType == QStringLiteral("list")) {
        refreshLobby(payload.value(QStringLiteral("rooms")).toArray()); return;
    }
    if (originalType == QStringLiteral("list_players")) {
        showTargetInviteDialog(payload.value(QStringLiteral("players")).toArray()); return;
    }
    if (originalType == QStringLiteral("diagnostics")) {
        const QJsonObject server = payload.value(QStringLiteral("diagnostics")).toObject();
        QStringList additions;
        const QStringList safeFields = {QStringLiteral("version"),
            QStringLiteral("server_time_ms"), QStringLiteral("connected_sockets"),
            QStringLiteral("database_open"), QStringLiteral("room_id"),
            QStringLiteral("room_status"), QStringLiteral("round_id"),
            QStringLiteral("seq"), QStringLiteral("room_paused")};
        for (const QString& key : safeFields) {
            if (server.contains(key) && !server.value(key).isObject()
                && !server.value(key).isArray())
                additions << QStringLiteral("server_%1=%2").arg(key,
                    server.value(key).toVariant().toString());
        }
        if (!additions.isEmpty()) {
            QString info = QApplication::clipboard()->text();
            if (info.startsWith(QStringLiteral("Feichuan Online Diagnostic v1"))) {
                info += additions.join(QLatin1Char('\n')) + QLatin1Char('\n');
                QApplication::clipboard()->setText(info);
                announce(QStringLiteral("服务器诊断摘要也已加入剪贴板"));
            }
        }
        return;
    }
    if (originalType == QStringLiteral("logout")) return;
    if (originalType == QStringLiteral("set_invite_accept")) {
        acceptsInvites_ = payload.value(QStringLiteral("enabled")).toBool(acceptsInvites_);
        return;
    }
    const QJsonObject view = payload.value(QStringLiteral("view")).toObject();
    if (!view.isEmpty()) refreshRoom(view);
    if (originalType == QStringLiteral("join")) {
        showRoom();
        announce(QStringLiteral("已加入 %1 房间").arg(roomId_));
    } else if (originalType == QStringLiteral("leave")) {
        roomId_.clear(); roundId_.clear(); seq_.clear(); roomView_ = {};
        refreshHand({});
        showLobby();
    } else if (originalType == QStringLiteral("invite")) {
        announce(QStringLiteral("邀请已发送"));
    } else if (originalType == QStringLiteral("invite_bots")) {
        announce(QStringLiteral("机器人已补齐空位"));
    } else if (originalType == QStringLiteral("set_password")
               || originalType == QStringLiteral("set_turn_seconds")) {
        announce(QStringLiteral("房间设置已更新"));
    }
}

void OnlineWindow::handleError(const QString& code, const QJsonObject& payload) {
    lastError_ = code;
    const QJsonObject view = payload.value(QStringLiteral("view")).toObject();
    if (!view.isEmpty()) refreshRoom(view);
    if (code == QStringLiteral("protocol_version") || code == QStringLiteral("version_mismatch")) {
        announce(QStringLiteral("服务器需要更新版本，请使用原有独立更新器升级"),
                 AnnouncementCategory::Error, AnnouncementPriority::Critical);
    } else {
        announce(errorText(code), AnnouncementCategory::Error, AnnouncementPriority::High);
    }
    if (recovering_) {
        if (code == QStringLiteral("account_connected") && resumeAttempts_ < 6) {
            announce(QStringLiteral("旧连接仍在释放，稍后继续尝试接回"));
            QTimer::singleShot(5500, this, [this]() {
                if (recovering_ && socket_.state() == QAbstractSocket::ConnectedState) {
                    ++resumeAttempts_;
                    send(QStringLiteral("resume"), {{QStringLiteral("token"), token_}});
                }
            });
            return;
        }
        recovering_ = false;
        resumeAttempts_ = 0;
        token_.clear();
        OnlineStorage::clearSession();
        showLogin();
    }
}

void OnlineWindow::showLogin() {
    pages_->setCurrentIndex(0);
    usernameEdit_->setFocus();
}

void OnlineWindow::showCount() {
    pages_->setCurrentIndex(1);
    pages_->currentWidget()->findChild<QPushButton*>()->setFocus();
}

void OnlineWindow::showLobby() {
    roomListTimer_.start();
    pages_->setCurrentIndex(2);
    lobbyTitle_->setText(QStringLiteral("%1 人房大厅，共 20 间固定房间").arg(playerCount_));
    lobbyList_->setFocus();
    send(QStringLiteral("list"), {{QStringLiteral("player_count"), playerCount_}});
}

void OnlineWindow::showRoom() {
    roomListTimer_.stop();
    pages_->setCurrentIndex(3);
    handView_->setFocus();
}

QString OnlineWindow::roomDescription(const QJsonObject& summary) const {
    return QStringLiteral("%1 号，%2 人房，已占 %3 席，%4，%5")
        .arg(roomNumber(summary.value(QStringLiteral("room_id")).toString()))
        .arg(summary.value(QStringLiteral("player_count")).toInt())
        .arg(summary.value(QStringLiteral("occupied")).toInt())
        .arg(summary.value(QStringLiteral("password_required")).toBool()
                 ? QStringLiteral("需要密码") : QStringLiteral("无密码"))
        .arg(statusText(summary.value(QStringLiteral("status")).toString()));
}

void OnlineWindow::refreshLobby(const QJsonArray& rooms) {
    if (rooms.isEmpty()) return;
    const std::vector<QJsonObject> sortedRooms = sortedLobbyRooms(rooms, playerCount_);
    if (sortedRooms.empty()) return;
    const QString selectedId = lobbyList_->currentItem()
        ? lobbyList_->currentItem()->data(Qt::UserRole).toString() : QString();
    roomSummaries_ = {};
    bool sameRooms = lobbyList_->count() == static_cast<int>(sortedRooms.size());
    for (int i = 0; i < static_cast<int>(sortedRooms.size()); ++i) {
        const QString id = sortedRooms[i].value(QStringLiteral("room_id")).toString();
        roomSummaries_.append(sortedRooms[i]);
        if (sameRooms && lobbyList_->item(i)->data(Qt::UserRole).toString() != id)
            sameRooms = false;
    }
    if (sameRooms) {
        for (int i = 0; i < static_cast<int>(sortedRooms.size()); ++i) {
            auto* item = lobbyList_->item(i);
            const QString description = roomDescription(sortedRooms[i]);
            if (item->text() != description) {
                item->setText(description);
                item->setToolTip(description);
            }
        }
        return;
    }
    lobbyList_->clear();
    int selectedRow = 0;
    for (int i = 0; i < static_cast<int>(sortedRooms.size()); ++i) {
        const QJsonObject& summary = sortedRooms[i];
        auto* item = new QListWidgetItem(roomDescription(summary), lobbyList_);
        const QString id = summary.value(QStringLiteral("room_id")).toString();
        item->setData(Qt::UserRole, id);
        item->setToolTip(item->text());
        if (id == selectedId) selectedRow = i;
    }
    lobbyList_->setCurrentRow(selectedRow);
}

void OnlineWindow::navigateRoom(int direction) {
    if (lobbyList_->count() == 0) return;
    const int current = std::max(0, lobbyList_->currentRow());
    const int next = (current + direction + lobbyList_->count()) % lobbyList_->count();
    lobbyList_->setCurrentRow(next);
}

void OnlineWindow::joinSelectedRoom(const QString& invitationId) {
    if (lobbyList_->count() == 0 || !lobbyList_->currentItem()) return;
    roomId_ = lobbyList_->currentItem()->data(Qt::UserRole).toString();
    QJsonObject payload;
    if (!invitationId.isEmpty()) payload[QStringLiteral("invitation_id")] = invitationId;
    else {
        QJsonObject summary;
        for (const QJsonValue& value : roomSummaries_)
            if (value.toObject().value(QStringLiteral("room_id")).toString() == roomId_)
                summary = value.toObject();
        seq_ = summary.value(QStringLiteral("seq")).toString();
        if (summary.value(QStringLiteral("password_required")).toBool()) {
            QDialog dialog(this);
            dialog.setWindowTitle(QStringLiteral("输入房间密码"));
            auto* layout = new QVBoxLayout(&dialog);
            auto* password = new QLineEdit(&dialog);
            password->setEchoMode(QLineEdit::Password);
            password->setAccessibleName(QStringLiteral("房间密码"));
            layout->addWidget(password);
            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            layout->addWidget(buttons);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            if (dialog.exec() != QDialog::Accepted) return;
            payload[QStringLiteral("password")] = password->text();
            password->clear();
        }
    }
    send(QStringLiteral("join"), payload, true);
}

void OnlineWindow::refreshRoom(const QJsonObject& view) {
    const QString receivedRoom = view.value(QStringLiteral("room_id")).toString();
    if (receivedRoom.isEmpty()) return;
    if (!roomId_.isEmpty() && roomId_ != receivedRoom) return;
    const QJsonObject previousView = roomView_;
    roomId_ = receivedRoom;
    roomView_ = visibleView(view);
    playerCount_ = view.value(QStringLiteral("player_count")).toInt(playerCount_);
    selfSeat_ = view.value(QStringLiteral("self_seat")).toInt(-1);
    const QJsonArray hand = view.value(QStringLiteral("hand")).toArray();
    if (selfSeat_ >= 0 && !view.contains(QStringLiteral("hand"))
        && view.value(QStringLiteral("status")).toString() == QStringLiteral("playing")) {
        lastError_ = QStringLiteral("invalid_private_view");
        socket_.close();
        announce(QStringLiteral("牌局视图缺少本家手牌，已断开并等待重同步"), AnnouncementCategory::Error);
        return;
    }
    if (view.contains(QStringLiteral("round_id")))
        roundId_ = view.value(QStringLiteral("round_id")).toString();
    seq_ = view.value(QStringLiteral("seq")).toString(seq_);
    const QString status = view.value(QStringLiteral("status")).toString();
    const int phase = view.value(QStringLiteral("phase")).toInt();
    const int turn = view.value(QStringLiteral("current_player")).toInt(-1);
    const bool myTurn = turn == selfSeat_ && selfSeat_ >= 0;
    const bool changedSeq = previousView.value(QStringLiteral("seq")).toString()
        != roomView_.value(QStringLiteral("seq")).toString();
    const bool myBiddingTurn = status == QStringLiteral("playing") && phase == 2 && myTurn;
    const bool myPlayingTurn = status == QStringLiteral("playing") && phase == 4 && myTurn;
    const QString bidTurn = roundId_ + QLatin1Char(':')
        + roomView_.value(QStringLiteral("deadline_ms")).toString();
    if (bidDialog_ && (!myBiddingTurn || lastBidPromptTurn_ != bidTurn))
        bidDialog_->reject();
    bidButton_->setVisible(myBiddingTurn);
    playButton_->setEnabled(myPlayingTurn);
    passButton_->setEnabled(myPlayingTurn
        && !roomView_.value(QStringLiteral("last_played_cards")).toArray().isEmpty());
    hintButton_->setEnabled(myPlayingTurn);
    if (changedSeq && status == QStringLiteral("playing")) {
        if (previousView.value(QStringLiteral("status")).toString() != QStringLiteral("playing"))
            sound_.play(SoundId::GameStart);
        else if (previousView.value(QStringLiteral("last_played_cards")).toArray()
                 != roomView_.value(QStringLiteral("last_played_cards")).toArray()
                 && !roomView_.value(QStringLiteral("last_played_cards")).toArray().isEmpty())
            sound_.play(SoundId::CardPlay);
    }
    QString details = QStringLiteral("%1 号房，%2；回合 %3 秒；%4")
        .arg(roomNumber(roomId_), statusText(status))
        .arg(view.value(QStringLiteral("turn_seconds")).toInt(15))
        .arg(phaseText(phase));
    if (status == QStringLiteral("playing")) {
        details += myTurn ? QStringLiteral("；轮到你") : QStringLiteral("；等待其他玩家");
        if (!view.value(QStringLiteral("record_eligible")).toBool(true))
            details += QStringLiteral("；本局战绩不计入账号");
        const qint64 ms = deadline(view) - QDateTime::currentMSecsSinceEpoch();
        if (ms > 0) details += QStringLiteral("；约剩 %1 秒").arg((ms + 999) / 1000);
    }
    if (status == QStringLiteral("finished")) {
        details += view.value(QStringLiteral("record_eligible")).toBool(true)
            ? QStringLiteral("；战绩计入账号") : QStringLiteral("；战绩不计入账号");
        const int winner = view.value(QStringLiteral("winner")).toInt(-1);
        if (winner >= 0) details += QStringLiteral("；胜者席位 %1").arg(winner + 1);
        const QJsonArray scores = view.value(QStringLiteral("score_changes")).toArray();
        if (selfSeat_ >= 0 && selfSeat_ < scores.size())
            details += QStringLiteral("；你的得分变化 %1").arg(scores[selfSeat_].toString());
    }
    roomStatus_->setText(details);
    seatList_->setVisible(status == QStringLiteral("waiting"));
    seatList_->clear();
    for (const QJsonValue& value : view.value(QStringLiteral("seats")).toArray()) {
        const QJsonObject seat = value.toObject();
        const QString nickname = seat.value(QStringLiteral("nickname")).toString();
        QString text = QStringLiteral("席位 %1，%2")
            .arg(seat.value(QStringLiteral("index")).toInt() + 1)
            .arg(nickname.isEmpty() ? QStringLiteral("空位") : nickname);
        if (seat.contains(QStringLiteral("remaining")))
            text += QStringLiteral("，剩 %1 张").arg(seat.value(QStringLiteral("remaining")).toInt());
        seatList_->addItem(text);
    }
    if (view.contains(QStringLiteral("recent_messages"))) {
        recentMessages_ = QJsonArray();
        const QJsonArray incoming = view.value(QStringLiteral("recent_messages")).toArray();
        for (qsizetype i = std::max<qsizetype>(0, incoming.size() - 20);
             i < incoming.size(); ++i)
            recentMessages_.append(visibleMessage(incoming[i].toObject()));
    }
    refreshHand(hand);
    if (status == QStringLiteral("finished")) {
        announce(details, AnnouncementCategory::Result, AnnouncementPriority::High);
        if (!roundId_.isEmpty() && lastShownResultRound_ != roundId_) {
            lastShownResultRound_ = roundId_;
            sound_.play(view.value(QStringLiteral("winner")).toInt(-1) == selfSeat_
                ? SoundId::Win : SoundId::Lose);
            const QJsonArray scores = view.value(QStringLiteral("score_changes")).toArray();
            QString summary = details + QLatin1Char('\n');
            const QJsonArray seats = view.value(QStringLiteral("seats")).toArray();
            for (int i = 0; i < scores.size() && i < seats.size(); ++i)
                summary += QStringLiteral("%1：%2 分\n")
                    .arg(seats[i].toObject().value(QStringLiteral("nickname")).toString(),
                         scores[i].toString());
            summary += QStringLiteral("如席位已满，房主按 F1 开始下一局。");
            QTimer::singleShot(0, this, [this, summary]() {
                QMessageBox::information(this, QStringLiteral("本局结果"), summary);
            });
        }
    }
    else if (myTurn && status == QStringLiteral("playing") && changedSeq
             && (previousView.value(QStringLiteral("current_player")).toInt(-1) != selfSeat_
                 || previousView.value(QStringLiteral("phase")).toInt(-1) != phase)) {
        announce(QStringLiteral("轮到你，%1").arg(phaseText(phase)), AnnouncementCategory::Turn,
                 AnnouncementPriority::High);
        sound_.play(SoundId::YourTurn);
    }
    if (myBiddingTurn && lastBidPromptTurn_ != bidTurn) {
        lastBidPromptTurn_ = bidTurn;
        QTimer::singleShot(0, this, [this, bidTurn]() {
            if (roomId_.isEmpty() || roundId_ + QLatin1Char(':')
                    + roomView_.value(QStringLiteral("deadline_ms")).toString() != bidTurn)
                return;
            bid();
        });
    }
}

void OnlineWindow::refreshHand(const QJsonArray& ids) {
    const int previous = handView_->currentIndex().row();
    std::vector<Card> cards;
    cards.reserve(size_t(ids.size()));
    QSet<int> present;
    for (const QJsonValue& value : ids) {
        const int id = value.toInt(-1);
        if (id < 0 || id >= TOTAL_CARDS || present.contains(id)) continue;
        Card card = Card::create(CardId(id));
        if (card.isValid()) { cards.push_back(card); present.insert(id); }
    }
    const bool changed = handModel_->setCards(cards);
    if (handModel_->rowCount() > 0 && (changed || !handView_->currentIndex().isValid())) {
        const int row = previous >= 0
            ? std::clamp(previous, 0, handModel_->rowCount() - 1)
            : handModel_->firstUnselectedRow();
        handView_->setCurrentIndex(handModel_->index(row, 0));
    }
    refreshSelection();
}

void OnlineWindow::refreshSelection() {
    if (!handModel_) return;
    const auto selected = handModel_->selectedCards();
    if (selected.empty()) {
        previewLabel_->setText(QStringLiteral("尚未选牌"));
    } else {
        const CardPattern pattern = PatternAnalyzer::analyze(selected, playerCount_);
        previewLabel_->setText(pattern.isValid()
            ? QStringLiteral("已选 %1 张，牌型 %2").arg(selected.size()).arg(
                  QString::fromStdWString(patternTypeName(pattern.type, playerCount_)))
            : QStringLiteral("已选 %1 张，尚非合法牌型").arg(selected.size()));
    }
    refreshTable();
}

void OnlineWindow::refreshTable() {
    if (!cardTable_ || !handModel_) return;
    PublicGameSnapshot snapshot;
    const int count = std::clamp(roomView_.value(QStringLiteral("player_count")).toInt(playerCount_),
                                 2, PLAYER_COUNT);
    const int ownSeat = selfSeat_ >= 0 && selfSeat_ < count ? selfSeat_ : 0;
    snapshot.activePlayerCount = count;
    snapshot.phase = roomView_.value(QStringLiteral("status")).toString() == QStringLiteral("waiting")
        ? GamePhase::NotStarted
        : static_cast<GamePhase>(roomView_.value(QStringLiteral("phase")).toInt());
    const int actor = roomView_.value(QStringLiteral("current_player")).toInt(-1);
    if (actor >= 0 && actor < count)
        snapshot.currentPlayer = static_cast<PlayerId>((actor - ownSeat + count) % count);
    const QJsonArray seats = roomView_.value(QStringLiteral("seats")).toArray();
    for (int relative = 0; relative < count; ++relative) {
        const int source = (ownSeat + relative) % count;
        if (source >= seats.size()) continue;
        const QJsonObject seat = seats[source].toObject();
        auto& player = snapshot.players[relative];
        player.id = static_cast<PlayerId>(relative);
        player.name = seat.value(QStringLiteral("nickname")).toString().toStdWString();
        player.remainingCards = seat.value(QStringLiteral("remaining")).toInt();
        player.role = static_cast<Role>(seat.value(QStringLiteral("role")).toInt());
        player.roleRevealed = roomView_.value(QStringLiteral("bottom_revealed")).toBool();
        player.bidScore = seat.value(QStringLiteral("bid")).toInt();
    }
    const auto appendCards = [](const QJsonArray& ids, std::vector<Card>& target) {
        for (const QJsonValue& value : ids) {
            const int id = value.toInt(-1);
            if (id >= 0 && id < TOTAL_CARDS) target.push_back(Card::create(CardId(id)));
        }
    };
    snapshot.bottomCardsRevealed = roomView_.value(QStringLiteral("bottom_revealed")).toBool();
    if (snapshot.bottomCardsRevealed)
        appendCards(roomView_.value(QStringLiteral("bottom_cards")).toArray(), snapshot.bottomCards);
    appendCards(roomView_.value(QStringLiteral("last_played_cards")).toArray(),
                snapshot.lastPlayedCards);
    snapshot.lastPlayedValid = !snapshot.lastPlayedCards.empty();
    const int lastPlayer = roomView_.value(QStringLiteral("last_played_by")).toInt(-1);
    if (lastPlayer >= 0 && lastPlayer < count)
        snapshot.lastPlayedBy = static_cast<PlayerId>((lastPlayer - ownSeat + count) % count);
    std::vector<Card> hand;
    std::vector<CardId> selected;
    for (int row = 0; row < handModel_->rowCount(); ++row) {
        const Card card = handModel_->cardAt(row);
        hand.push_back(card);
        if (handModel_->isSelected(row)) selected.push_back(card.id());
    }
    cardTable_->setTableState(snapshot, hand, selected);
}

void OnlineWindow::moveHandCursorTo(int row) {
    if (!handModel_ || !handView_ || !handView_->selectionModel()) return;
    const QModelIndex index = handModel_->index(row, 0);
    if (!index.isValid()) return;
    handModel_->setAccessibilityTextSuppressed(false);
    {
        const QSignalBlocker blocker(handView_->selectionModel());
        handView_->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
    }
    handView_->scrollTo(index, QAbstractItemView::EnsureVisible);
    handView_->viewport()->update();
    const QString spoken = handModel_->data(index, Qt::AccessibleTextRole).toString();
    if (!spoken.isEmpty()) announce(spoken, AnnouncementCategory::CardNavigation,
                                     AnnouncementPriority::Low);
}

void OnlineWindow::takeCurrentCard() {
    if (!handModel_ || !handView_->currentIndex().isValid()) return;
    handModel_->setAccessibilityTextSuppressed(true);
    int row = handView_->currentIndex().row();
    if (handModel_->isSelected(row)) {
        const int next = handModel_->nextUnselectedRow(row);
        if (next >= 0) row = next;
    }
    if (handModel_->isSelected(row) || !handModel_->selectSingle(row)) return;
    handModel_->completeEndpointSelection(playerCount_, true);
    const QModelIndex index = handModel_->index(row, 0);
    {
        const QSignalBlocker blocker(handView_->selectionModel());
        handView_->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
    }
    handView_->scrollTo(index, QAbstractItemView::EnsureVisible);
    handView_->viewport()->update();
    sound_.play(SoundId::CardSelect);
    const auto selected = handModel_->selectedCards();
    const CardPattern pattern = PatternAnalyzer::analyze(selected, playerCount_);
    const bool sequence = pattern.isValid() &&
        (pattern.type == CardPatternType::Straight ||
         pattern.type == CardPatternType::ConsecutivePairs ||
         pattern.type == CardPatternType::Airplane);
    announce(sequence
        ? QString::fromStdWString(CardTextFormatter::formatPlayedCards(pattern, selected))
        : QString::fromStdWString(CardTextFormatter::formatRankSpeech(
              handModel_->cardAt(row).rank())), AnnouncementCategory::CardSelection);
}

void OnlineWindow::takeCurrentGroup() {
    if (!handModel_ || !handView_->currentIndex().isValid()) return;
    int row = handModel_->groupStartRow(handView_->currentIndex().row());
    if (row < 0) return;
    handModel_->setAccessibilityTextSuppressed(true);
    auto result = handModel_->selectGroup(row);
    if (!result.valid) return;
    if (result.newlySelectedCount == 0) {
        const int next = handModel_->nextGroupStartRow(row);
        if (next != row) { row = next; result = handModel_->selectGroup(row); }
    }
    const QModelIndex index = handModel_->index(row, 0);
    {
        const QSignalBlocker blocker(handView_->selectionModel());
        handView_->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
    }
    handView_->scrollTo(index, QAbstractItemView::EnsureVisible);
    handView_->viewport()->update();
    if (result.newlySelectedCount == 0) return;
    handModel_->completeEndpointSelection(playerCount_, false);
    sound_.play(SoundId::CardSelect);
    const auto selected = handModel_->selectedCards();
    const CardPattern pattern = PatternAnalyzer::analyze(selected, playerCount_);
    const bool sequence = pattern.isValid() &&
        (pattern.type == CardPatternType::Straight ||
         pattern.type == CardPatternType::ConsecutivePairs ||
         pattern.type == CardPatternType::Airplane);
    announce(sequence
        ? QString::fromStdWString(CardTextFormatter::formatPlayedCards(pattern, selected))
        : QString::fromStdWString(CardTextFormatter::formatSameRankSpeech(
              result.rank, result.groupCount)), AnnouncementCategory::CardSelection);
}

void OnlineWindow::putDownCurrentCard() {
    if (!handModel_) return;
    handModel_->setAccessibilityTextSuppressed(true);
    std::optional<Card> card;
    const QModelIndex index = handView_->currentIndex();
    if (index.isValid() && handModel_->isSelected(index.row())) {
        card = handModel_->cardAt(index.row());
        handModel_->setSelected(index.row(), false);
    } else {
        card = handModel_->deselectNextPickedCard();
    }
    if (!card) return;
    sound_.play(SoundId::CardDeselect);
    announce(QString::fromStdWString(CardTextFormatter::formatRankSpeech(card->rank())),
             AnnouncementCategory::CardSelection);
}

void OnlineWindow::putDownAllCards() {
    if (!handModel_ || handModel_->selectedCount() == 0) return;
    const auto cards = handModel_->selectedCards();
    handModel_->setAccessibilityTextSuppressed(true);
    handModel_->clearSelection();
    sound_.play(SoundId::CardDeselect);
    const CardPattern pattern = PatternAnalyzer::analyze(cards, playerCount_);
    const bool sequence = pattern.isValid() &&
        (pattern.type == CardPatternType::Straight ||
         pattern.type == CardPatternType::ConsecutivePairs ||
         pattern.type == CardPatternType::Airplane);
    announce(sequence
        ? QString::fromStdWString(CardTextFormatter::formatPlayedCards(pattern, cards))
        : QString::fromStdWString(CardTextFormatter::formatCards(cards)),
        AnnouncementCategory::CardSelection);
}

void OnlineWindow::showHint() {
    if (!isRoundActive() || roomView_.value(QStringLiteral("phase")).toInt() != 4
        || roomView_.value(QStringLiteral("current_player")).toInt(-1) != selfSeat_)
        return;
    Hand hand;
    std::vector<Card> ownCards;
    for (int row = 0; row < handModel_->rowCount(); ++row)
        ownCards.push_back(handModel_->cardAt(row));
    hand.addCards(ownCards);
    std::vector<Card> lastCards;
    for (const QJsonValue& value : roomView_.value(QStringLiteral("last_played_cards")).toArray()) {
        const int id = value.toInt(-1);
        if (id >= 0 && id < TOTAL_CARDS) lastCards.push_back(Card::create(CardId(id)));
    }
    std::vector<Card> hint;
    if (lastCards.empty()) {
        auto moves = LegalMoveGenerator::generateFreePlayMoves(hand, playerCount_);
        if (!moves.empty()) hint = std::move(moves.front());
    } else {
        const CardPattern lastPattern = PatternAnalyzer::analyze(lastCards, playerCount_);
        auto moves = LegalMoveGenerator::generateResponseMoves(hand, lastPattern, playerCount_);
        for (const auto& move : moves) {
            const CardPattern pattern = PatternAnalyzer::analyze(move, playerCount_);
            if (pattern.isValid() && PatternComparator::canBeat(pattern, lastPattern)) {
                hint = move;
                break;
            }
        }
    }
    if (hint.empty()) {
        announce(QStringLiteral("当前没有可以压过上一手的牌，可以按Control加回车键过牌"));
        sound_.play(SoundId::Invalid);
        return;
    }
    handModel_->setAccessibilityTextSuppressed(true);
    handModel_->clearSelection();
    for (int row = 0; row < handModel_->rowCount(); ++row) {
        if (std::any_of(hint.cbegin(), hint.cend(), [this, row](const Card& card) {
                return card.id() == handModel_->cardAt(row).id();
            })) handModel_->setSelected(row, true);
    }
    sound_.play(SoundId::CardSelect);
    announce(QStringLiteral("提示牌，共%1张，按回车出牌").arg(hint.size()),
             AnnouncementCategory::CardSelection);
}

QString OnlineWindow::visibleCardNames(const QJsonArray& ids) const {
    std::vector<Card> cards;
    for (const QJsonValue& value : ids) {
        const int id = value.toInt(-1);
        if (id >= 0 && id < TOTAL_CARDS)
            cards.push_back(Card::create(CardId(id)));
    }
    return QString::fromStdWString(CardTextFormatter::formatPublicCards(cards));
}

void OnlineWindow::startRound() { send(QStringLiteral("start"), {}, true); }
void OnlineWindow::bid() {
    if (!isRoundActive() || roomView_.value(QStringLiteral("phase")).toInt() != 2
        || roomView_.value(QStringLiteral("current_player")).toInt(-1) != selfSeat_
        || bidDialog_) return;
    const QString turn = roundId_ + QLatin1Char(':')
        + roomView_.value(QStringLiteral("deadline_ms")).toString();
    int highest = 0;
    for (const QJsonValue& seat : roomView_.value(QStringLiteral("seats")).toArray())
        highest = std::max(highest, seat.toObject().value(QStringLiteral("bid")).toInt());
    QDialog dialog(this);
    bidDialog_ = &dialog;
    dialog.setWindowTitle(QStringLiteral("叫分"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(QStringLiteral("轮到你叫分，当前最高 %1 分，请选择：")
        .arg(highest), &dialog));
    auto* choices = new QHBoxLayout;
    for (int score = 0; score <= 3; ++score) {
        auto* button = makeButton(score == 0 ? QStringLiteral("不叫")
            : QStringLiteral("%1 分").arg(score), &dialog);
        button->setEnabled(score == 0 || score > highest);
        if (score == 0) button->setFocus();
        choices->addWidget(button);
        connect(button, &QPushButton::clicked, &dialog,
                [&dialog, score]() { dialog.done(score + 1); });
        auto* shortcut = new QShortcut(QKeySequence(Qt::Key_0 + score), &dialog);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, button, [button]() {
            if (button->isEnabled()) button->click();
        });
    }
    layout->addLayout(choices);
    auto* cancel = makeButton(QStringLiteral("取消"), &dialog);
    layout->addWidget(cancel);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    const int choice = dialog.exec();
    bidDialog_ = nullptr;
    if (choice >= 1 && choice <= 4 && isRoundActive()
        && roomView_.value(QStringLiteral("phase")).toInt() == 2
        && roomView_.value(QStringLiteral("current_player")).toInt(-1) == selfSeat_
        && roundId_ + QLatin1Char(':')
            + roomView_.value(QStringLiteral("deadline_ms")).toString() == turn)
        send(QStringLiteral("bid"), {{QStringLiteral("score"), choice - 1}}, true);
}
void OnlineWindow::playSelected() {
    if (!isRoundActive() || roomView_.value(QStringLiteral("phase")).toInt() != 4
        || roomView_.value(QStringLiteral("current_player")).toInt(-1) != selfSeat_) return;
    const auto selected = handModel_->selectedCardIds();
    if (selected.empty()) {
        announce(QStringLiteral("请先选择要出的牌"), AnnouncementCategory::Error); return;
    }
    QJsonArray ids;
    for (CardId id : selected) ids.append(int(id));
    send(QStringLiteral("play"), {{QStringLiteral("card_ids"), ids}}, true);
}
void OnlineWindow::pass() {
    if (isRoundActive() && roomView_.value(QStringLiteral("phase")).toInt() == 4
        && roomView_.value(QStringLiteral("current_player")).toInt(-1) == selfSeat_)
        send(QStringLiteral("pass"), {}, true);
}

bool OnlineWindow::isRoundActive() const {
    return roomView_.value(QStringLiteral("status")).toString() == QStringLiteral("playing");
}

void OnlineWindow::leaveRoom() {
    if (isRoundActive() && QMessageBox::question(this, QStringLiteral("离开牌局"),
            QStringLiteral("离开进行中的牌局后，本席会交给托管机器人，本局战绩不计入。确定离开？"))
            != QMessageBox::Yes) return;
    send(QStringLiteral("leave"), {}, true);
}

void OnlineWindow::announce(const QString& text, AnnouncementCategory category,
                            AnnouncementPriority priority) {
    announcementLabel_->setText(text);
    Announcement item;
    item.text = text.toStdWString();
    item.category = category;
    item.priority = priority;
    accessibility_.announce(item, announcementLabel_);
}

QString OnlineWindow::errorText(const QString& code) const {
    static const QMap<QString, QString> messages{
        {QStringLiteral("full"), QStringLiteral("房间已满")},
        {QStringLiteral("room_full"), QStringLiteral("房间已满")},
        {QStringLiteral("wrong_password"), QStringLiteral("房间密码错误")},
        {QStringLiteral("password_invalid"), QStringLiteral("房间密码错误")},
        {QStringLiteral("rate_limited"), QStringLiteral("操作过于频繁，请稍后再试")},
        {QStringLiteral("chat_limited"), QStringLiteral("消息发送过于频繁")},
        {QStringLiteral("stale_seq"), QStringLiteral("牌局状态已变化，已重新同步")},
        {QStringLiteral("invalid_invitation"), QStringLiteral("邀请已失效或房间已满")},
        {QStringLiteral("invitation_expired"), QStringLiteral("邀请已过期，请让房主重新发送")},
        {QStringLiteral("invalid_credentials"), QStringLiteral("用户名或密码错误")},
        {QStringLiteral("account_in_use"), QStringLiteral("账号已在另一处登录")},
        {QStringLiteral("account_connected"), QStringLiteral("此账号旧连接仍在使用，请稍后再试")},
        {QStringLiteral("invalid_session"), QStringLiteral("记住登录已失效，请重新输入账号密码")},
        {QStringLiteral("auth_limited"), QStringLiteral("登录尝试过于频繁，请一分钟后再试")},
        {QStringLiteral("password_limited"), QStringLiteral("房间密码尝试过于频繁，请一分钟后再试")},
        {QStringLiteral("database_unavailable"), QStringLiteral("服务器暂时无法保存牌局，请稍后重试")},
        {QStringLiteral("room_paused"), QStringLiteral("房间因服务器存储故障暂停，请稍后重试")},
        {QStringLiteral("invalid_action"), QStringLiteral("当前操作不符合牌局规则")},
        {QStringLiteral("illegal_action"), QStringLiteral("所选牌不符合当前出牌规则")},
        {QStringLiteral("not_actor"), QStringLiteral("还没轮到你")},
        {QStringLiteral("bot_deciding"), QStringLiteral("这一手已交给托管机器人，稍后会同步最新牌局")},
        {QStringLiteral("too_many_cards"), QStringLiteral("一次出牌张数超出本局手牌上限")},
        {QStringLiteral("not_turn"), QStringLiteral("还没轮到你")},
        {QStringLiteral("host_required"), QStringLiteral("只有房主可以更改此设置")},
        {QStringLiteral("host_alone_required"), QStringLiteral("只有房主独自在房且未开局时可以设置密码")},
        {QStringLiteral("host_or_seats_required"), QStringLiteral("须由房主在席位满员时开始牌局")},
        {QStringLiteral("invite_limited"), QStringLiteral("邀请发送过于频繁，请稍后再试")},
        {QStringLiteral("recipient_unavailable"), QStringLiteral("指定玩家目前不在可邀请大厅名单")}
    };
    return messages.value(code, QStringLiteral("联机操作失败：%1").arg(code.isEmpty()
        ? QStringLiteral("未知错误") : code));
}

bool OnlineWindow::handleRoomKey(QKeyEvent* event) {
    if (pages_->currentIndex() != 3 || QApplication::activeModalWidget()) return false;
    const auto modifiers = event->modifiers() & ~Qt::KeypadModifier;
    const int key = event->key();
    const bool plain = modifiers == Qt::NoModifier;
    const bool control = modifiers == Qt::ControlModifier;
    const bool browsing = (key == Qt::Key_Left || key == Qt::Key_Right)
        && (plain || control);
    if (event->isAutoRepeat() && !browsing) return true;
    if ((key == Qt::Key_Left || key == Qt::Key_Right)
        && modifiers == Qt::ShiftModifier) return true;
    if ((key == Qt::Key_Q && control) || (key == Qt::Key_X && modifiers == Qt::AltModifier)) {
        close(); return true;
    }
    if (key == Qt::Key_Escape && plain) { leaveRoom(); return true; }
    if (key == Qt::Key_H && modifiers == Qt::AltModifier) {
        showHint(); return true;
    }
    if (key == Qt::Key_E && modifiers == Qt::AltModifier) {
        playSelected(); return true;
    }
    if (key == Qt::Key_P && modifiers == Qt::AltModifier) {
        pass(); return true;
    }
    if (key == Qt::Key_F11 && plain) {
        if (isRoundActive()) {
            const int actor = roomView_.value(QStringLiteral("current_player")).toInt(-1);
            const QJsonArray seats = roomView_.value(QStringLiteral("seats")).toArray();
            if (actor >= 0 && actor < seats.size())
                announce(seats[actor].toObject().value(QStringLiteral("nickname")).toString(),
                         AnnouncementCategory::Turn);
        }
        return true;
    }
    if (key == Qt::Key_F12 && plain) {
        const QJsonArray ids = roomView_.value(QStringLiteral("last_played_cards")).toArray();
        const QJsonArray seats = roomView_.value(QStringLiteral("seats")).toArray();
        const int actor = roomView_.value(QStringLiteral("last_played_by")).toInt(-1);
        if (ids.isEmpty() || actor < 0 || actor >= seats.size()) {
            announce(QStringLiteral("空"));
        } else {
            std::vector<Card> cards;
            for (const QJsonValue& value : ids) {
                const int id = value.toInt(-1);
                if (id >= 0 && id < TOTAL_CARDS)
                    cards.push_back(Card::create(CardId(id)));
            }
            const CardPattern pattern = PatternAnalyzer::analyze(cards, playerCount_);
            announce(seats[actor].toObject().value(QStringLiteral("nickname")).toString()
                + QStringLiteral("，")
                + QString::fromStdWString(CardTextFormatter::formatPlayedCards(
                    pattern, cards, playerCount_)));
        }
        return true;
    }
    if (key == Qt::Key_F && modifiers == Qt::AltModifier) {
        announce(QStringLiteral("基础分：%1，当前倍数：%2")
            .arg(roomView_.value(QStringLiteral("base_score")).toInt())
            .arg(roomView_.value(QStringLiteral("multiplier")).toString(QStringLiteral("1"))));
        return true;
    }
    if (key >= Qt::Key_1 && key <= Qt::Key_4 && plain
        && !event->modifiers().testFlag(Qt::KeypadModifier)) {
        const int relative = key - Qt::Key_1;
        const int count = roomView_.value(QStringLiteral("player_count")).toInt(playerCount_);
        if (relative >= count) {
            announce(QStringLiteral("当前%1人模式没有%2号玩家").arg(count).arg(relative + 1));
            return true;
        }
        const QJsonArray seats = roomView_.value(QStringLiteral("seats")).toArray();
        const int source = (std::max(0, selfSeat_) + relative) % count;
        if (source < seats.size()) {
            const QJsonObject seat = seats[source].toObject();
            QString message = seat.value(QStringLiteral("nickname")).toString();
            if (relative == 0) message += QStringLiteral("，自己");
            message += QStringLiteral("，剩余%1张牌")
                .arg(seat.value(QStringLiteral("remaining")).toInt());
            const int role = seat.value(QStringLiteral("role")).toInt();
            message += role == int(Role::Landlord) ? QStringLiteral("，地主")
                : role == int(Role::Farmer) ? QStringLiteral("，农民")
                : QStringLiteral("，身份未定");
            announce(message);
        }
        return true;
    }
    if (handModel_ && handModel_->rowCount() > 0) {
        const int row = handView_->currentIndex().isValid()
            ? handView_->currentIndex().row() : 0;
        if (browsing) {
            int target = -1;
            if (key == Qt::Key_Left)
                target = plain ? handModel_->previousBrowsableGroupStartRow(row)
                               : handModel_->previousBrowsableMultiCardGroupStartRow(row);
            else
                target = plain ? handModel_->nextBrowsableGroupStartRow(row)
                               : handModel_->nextBrowsableMultiCardGroupStartRow(row);
            if (target >= 0) moveHandCursorTo(target);
            return true;
        }
        if (key == Qt::Key_Home && plain) {
            moveHandCursorTo(handModel_->firstUnselectedRow()); return true;
        }
        if (key == Qt::Key_End && plain) {
            moveHandCursorTo(handModel_->lastBrowsableGroupStartRow()); return true;
        }
        if (isRoundActive() && roomView_.value(QStringLiteral("phase")).toInt() == 4) {
            if (key == Qt::Key_Up && plain) { takeCurrentCard(); return true; }
            if (key == Qt::Key_Up && control) { takeCurrentGroup(); return true; }
            if (key == Qt::Key_Down && plain) { putDownCurrentCard(); return true; }
            if (key == Qt::Key_Down && control) { putDownAllCards(); return true; }
        }
    }
    const bool myPlayTurn = isRoundActive()
        && roomView_.value(QStringLiteral("phase")).toInt() == 4
        && roomView_.value(QStringLiteral("current_player")).toInt(-1) == selfSeat_;
    if (myPlayTurn && key == Qt::Key_Space && plain) return true;
    if (myPlayTurn && (key == Qt::Key_Return || key == Qt::Key_Enter)) {
        if (plain) { playSelected(); return true; }
        if (control) { pass(); return true; }
    }
    return false;
}

bool OnlineWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (watched == lobbyList_) {
            if (key->key() == Qt::Key_Left) { navigateRoom(-1); return true; }
            if (key->key() == Qt::Key_Right) { navigateRoom(1); return true; }
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                joinSelectedRoom(); return true;
            }
        }
        auto* widget = qobject_cast<QWidget*>(watched);
        if (pages_->currentIndex() == 3 && widget && widget->window() == this &&
            handleRoomKey(key)) return true;
    }
    return QWidget::eventFilter(watched, event);
}

void OnlineWindow::keyPressEvent(QKeyEvent* event) {
    if (pages_->currentIndex() == 3) {
        switch (event->key()) {
        case Qt::Key_F1: startRound(); return;
        case Qt::Key_F2:
            announce(roomView_.value(QStringLiteral("bottom_revealed")).toBool()
                ? QStringLiteral("已公开底牌：")
                    + visibleCardNames(roomView_.value(QStringLiteral("bottom_cards")).toArray())
                : QStringLiteral("底牌尚未公开"));
            return;
        case Qt::Key_F3: showEmojiDialog(); return;
        case Qt::Key_F4: showChatDialog(); return;
        case Qt::Key_F5: showSettingsDialog(); return;
        default: break;
        }
        if (handleRoomKey(event)) return;
    }
    QWidget::keyPressEvent(event);
}

void OnlineWindow::closeEvent(QCloseEvent* event) {
    if (isRoundActive() && !closing_) {
        if (QMessageBox::question(this, QStringLiteral("退出在线版"),
                QStringLiteral("退出后本席会交给托管机器人，本局战绩不计入。确定退出？"))
            != QMessageBox::Yes) { event->ignore(); return; }
    }
    closing_ = true;
    if (!roomId_.isEmpty() && socket_.state() == QAbstractSocket::ConnectedState)
        send(QStringLiteral("leave"), {}, true);
    socket_.flush();
    event->accept();
}

void OnlineWindow::copyDiagnostics() {
    const QString state = socket_.state() == QAbstractSocket::ConnectedState
        ? QStringLiteral("connected") : QStringLiteral("disconnected");
    const QString info = QStringLiteral("Feichuan Online Diagnostic v1\napp_version=%1\n"
        "utc=%2\nnetwork=%3\npage=%4\nroom_id=%5\nround_id=%6\nseq=%7\n"
        "last_error=%8\nreader=%9\n")
        .arg(QStringLiteral(FPDZ_APP_VERSION),
             QDateTime::currentDateTimeUtc().toString(Qt::ISODate), state,
             QString::number(pages_->currentIndex()), roomId_, roundId_, seq_, lastError_,
             QString::fromStdWString(accessibility_.routeName()));
    QApplication::clipboard()->setText(info);
    announce(QStringLiteral("已复制在线诊断信息；内容不含密码、令牌、手牌和消息正文"));
    if (!username_.isEmpty() && socket_.state() == QAbstractSocket::ConnectedState)
        send(QStringLiteral("diagnostics"));
}

void OnlineWindow::playEmojiSound(int id) {
    const QString path = OnlineStorage::emojiSoundPath(id);
    if (!path.isEmpty()) PlaySoundW(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr,
                                   SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
}

void OnlineWindow::showEmojiDialog() {
    if (roomId_.isEmpty()) return;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("发送房内表情"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(QStringLiteral("左右方向键选择表情，回车发送，Esc 取消"), &dialog));
    auto* list = new QListWidget(&dialog);
    list->setAccessibleName(QStringLiteral("固定表情列表"));
    for (int i = 0; i < kEmojiNames.size(); ++i)
        list->addItem(QStringLiteral("%1，%2").arg(kEmojiNames[i],
            OnlineStorage::emojiSoundPath(i + 1).isEmpty()
                ? QStringLiteral("未导入音效") : QStringLiteral("有自定义音效")));
    list->setCurrentRow(0);
    layout->addWidget(list);
    auto* sendButton = makeButton(QStringLiteral("发送"), &dialog);
    layout->addWidget(sendButton);
    connect(sendButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(list, &QListWidget::itemDoubleClicked, &dialog,
            [&dialog](QListWidgetItem*) { dialog.accept(); });
    // An event filter limited to this dialog keeps the main hand shortcuts intact.
    class EmojiKeys final : public QObject {
    public:
        explicit EmojiKeys(QListWidget* items, QDialog* owner) : QObject(owner), items_(items), owner_(owner) {}
        bool eventFilter(QObject*, QEvent* event) override {
            if (event->type() != QEvent::KeyPress) return false;
            auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Left || key->key() == Qt::Key_Right) {
                const int direction = key->key() == Qt::Key_Right ? 1 : -1;
                items_->setCurrentRow((items_->currentRow() + direction + items_->count()) % items_->count());
                return true;
            }
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                owner_->accept(); return true;
            }
            return false;
        }
    private:
        QListWidget* items_;
        QDialog* owner_;
    } keys(list, &dialog);
    list->installEventFilter(&keys);
    list->setFocus();
    if (dialog.exec() == QDialog::Accepted && list->currentRow() >= 0) {
        send(QStringLiteral("emote"), {{QStringLiteral("id"), list->currentRow() + 1}}, true);
    }
}

void OnlineWindow::showChatDialog() {
    if (roomId_.isEmpty()) return;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("房内文字消息"));
    dialog.resize(560, 420);
    auto* layout = new QVBoxLayout(&dialog);
    auto* list = new QListWidget(&dialog);
    list->setAccessibleName(QStringLiteral("最近 20 条房内消息"));
    for (const QJsonValue& value : recentMessages_) {
        const QJsonObject message = value.toObject();
        list->addItem(message.value(QStringLiteral("nickname")).toString()
            + QStringLiteral("：") + message.value(QStringLiteral("text")).toString());
    }
    layout->addWidget(list, 1);
    auto* input = new QLineEdit(&dialog);
    input->setMaxLength(200);
    input->setAccessibleName(QStringLiteral("输入房内消息，最多 200 个可显示字符，回车发送"));
    layout->addWidget(input);
    auto* buttons = new QHBoxLayout;
    auto* sendButton = makeButton(QStringLiteral("发送消息"), &dialog);
    auto* closeButton = makeButton(QStringLiteral("关闭"), &dialog);
    buttons->addWidget(sendButton);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);
    auto sendMessage = [this, input]() {
        const QString body = input->text().trimmed();
        if (body.isEmpty()) return;
        send(QStringLiteral("chat"), {{QStringLiteral("text"), body}}, true);
        input->clear();
    };
    connect(input, &QLineEdit::returnPressed, &dialog, sendMessage);
    connect(sendButton, &QPushButton::clicked, &dialog, sendMessage);
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    chatDialog_ = &dialog;
    chatMessages_ = list;
    chatInput_ = input;
    input->setFocus();
    dialog.exec();
    chatDialog_.clear();
    chatMessages_ = nullptr;
    chatInput_ = nullptr;
}

void OnlineWindow::showSettingsDialog() {
    if (roomId_.isEmpty()) return;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("在线设置"));
    dialog.resize(590, 540);
    auto* layout = new QVBoxLayout(&dialog);
    const bool host = roomView_.value(QStringLiteral("is_host")).toBool()
        || roomView_.value(QStringLiteral("host")).toString()
            .compare(username_, Qt::CaseInsensitive) == 0;
    const bool waiting = roomView_.value(QStringLiteral("status")).toString() == QStringLiteral("waiting");
    auto* inviteAll = makeButton(QStringLiteral("向全服发邀请"), &dialog);
    auto* inviteTarget = makeButton(QStringLiteral("邀请指定玩家"), &dialog);
    auto* inviteBots = makeButton(QStringLiteral("邀请机器人补齐空位"), &dialog);
    inviteBots->setEnabled(host && waiting
        && roomView_.value(QStringLiteral("occupied")).toInt()
            < roomView_.value(QStringLiteral("player_count")).toInt());
    layout->addWidget(inviteAll);
    layout->addWidget(inviteTarget);
    layout->addWidget(inviteBots);
    connect(inviteAll, &QPushButton::clicked, &dialog, [this]() {
        send(QStringLiteral("invite"), {{QStringLiteral("scope"), QStringLiteral("all")}}, true);
    });
    connect(inviteTarget, &QPushButton::clicked, &dialog, [this, &dialog]() {
        dialog.accept();
        send(QStringLiteral("list_players"));
    });
    connect(inviteBots, &QPushButton::clicked, &dialog, [this, &dialog]() {
        dialog.accept();
        send(QStringLiteral("invite_bots"), {}, true);
    });
    auto* acceptInvites = new QCheckBox(QStringLiteral("接收邀请"), &dialog);
    acceptInvites->setChecked(acceptsInvites_);
    layout->addWidget(acceptInvites);
    connect(acceptInvites, &QCheckBox::toggled, &dialog, [this](bool checked) {
        send(QStringLiteral("set_invite_accept"), {{QStringLiteral("enabled"), checked}});
    });
    auto* passwordLabel = new QLabel(QStringLiteral("房间密码：仅房主独自在房、未开局时可设置或更换"), &dialog);
    passwordLabel->setWordWrap(true);
    layout->addWidget(passwordLabel);
    auto* password = new QLineEdit(&dialog);
    password->setEchoMode(QLineEdit::Password);
    password->setAccessibleName(QStringLiteral("新房间密码，不显示已有密码"));
    password->setEnabled(host && waiting && roomView_.value(QStringLiteral("occupied")).toInt() == 1);
    layout->addWidget(password);
    auto* savePassword = makeButton(QStringLiteral("设置房间密码"), &dialog);
    savePassword->setEnabled(password->isEnabled());
    layout->addWidget(savePassword);
    connect(savePassword, &QPushButton::clicked, &dialog, [this, password]() {
        if (password->text().isEmpty()) return;
        send(QStringLiteral("set_password"), {{QStringLiteral("password"), password->text()}}, true);
        password->clear();
    });
    auto* turns = new QComboBox(&dialog);
    for (int seconds : {15, 30, 60, 120})
        turns->addItem(QStringLiteral("%1 秒").arg(seconds), seconds);
    turns->setCurrentIndex(std::max(0, turns->findData(
        roomView_.value(QStringLiteral("turn_seconds")).toInt(15))));
    turns->setAccessibleName(QStringLiteral("回合时限，允许 15、30、60 或 120 秒"));
    turns->setEnabled(host && waiting);
    layout->addWidget(turns);
    auto* saveTurns = makeButton(QStringLiteral("设置回合时限"), &dialog);
    saveTurns->setEnabled(turns->isEnabled());
    layout->addWidget(saveTurns);
    connect(saveTurns, &QPushButton::clicked, &dialog, [this, turns]() {
        send(QStringLiteral("set_turn_seconds"),
             {{QStringLiteral("seconds"), turns->currentData().toInt()}}, true);
    });
    auto* soundButton = makeButton(QStringLiteral("表情音效：导入和管理自定义文件"), &dialog);
    layout->addWidget(soundButton);
    connect(soundButton, &QPushButton::clicked, &dialog, [this]() {
        QDialog sounds(this);
        sounds.setWindowTitle(QStringLiteral("自定义表情音效"));
        auto* soundLayout = new QVBoxLayout(&sounds);
        soundLayout->addWidget(new QLabel(QStringLiteral("可为每种表情导入自己制作的 PCM WAV 音效。未导入时仍可发送文字表情。"), &sounds));
        auto* list = new QListWidget(&sounds);
        for (int i = 0; i < kEmojiNames.size(); ++i)
            list->addItem(kEmojiNames[i] + (OnlineStorage::emojiSoundPath(i + 1).isEmpty()
                ? QStringLiteral("，未导入") : QStringLiteral("，已导入")));
        list->setCurrentRow(0);
        list->setAccessibleName(QStringLiteral("表情与自定义音效对应列表"));
        soundLayout->addWidget(list);
        auto* importButton = makeButton(QStringLiteral("为所选表情导入 WAV"), &sounds);
        auto* previewButton = makeButton(QStringLiteral("试听"), &sounds);
        auto* removeButton = makeButton(QStringLiteral("移除所选自定义音效"), &sounds);
        soundLayout->addWidget(importButton);
        soundLayout->addWidget(previewButton);
        soundLayout->addWidget(removeButton);
        connect(importButton, &QPushButton::clicked, &sounds, [this, list]() {
            const int id = list->currentRow() + 1;
            const QString source = QFileDialog::getOpenFileName(this, QStringLiteral("导入表情音效"),
                QString(), QStringLiteral("WAV 音频 (*.wav)"));
            if (source.isEmpty()) return;
            QString error;
            if (!SoundService::validateWaveFile(source, &error)
                || !OnlineStorage::importEmojiSound(id, source, &error)) {
                announce(QStringLiteral("导入失败：") + error, AnnouncementCategory::Error); return;
            }
            list->currentItem()->setText(kEmojiNames[id - 1] + QStringLiteral("，已导入"));
            announce(QStringLiteral("已为 %1 导入自定义音效").arg(kEmojiNames[id - 1]));
        });
        connect(previewButton, &QPushButton::clicked, &sounds, [this, list]() {
            const int id = list->currentRow() + 1;
            if (OnlineStorage::emojiSoundPath(id).isEmpty()) {
                announce(QStringLiteral("该表情尚未导入音效")); return;
            }
            playEmojiSound(id);
        });
        connect(removeButton, &QPushButton::clicked, &sounds, [this, list]() {
            const int id = list->currentRow() + 1;
            OnlineStorage::removeEmojiSound(id);
            list->currentItem()->setText(kEmojiNames[id - 1] + QStringLiteral("，未导入"));
            announce(QStringLiteral("已移除 %1 的自定义音效").arg(kEmojiNames[id - 1]));
        });
        sounds.exec();
    });
    auto* diagnostics = makeButton(QStringLiteral("复制在线诊断信息"), &dialog);
    layout->addWidget(diagnostics);
    connect(diagnostics, &QPushButton::clicked, &dialog, [this]() { copyDiagnostics(); });
    auto* closeButton = makeButton(QStringLiteral("关闭设置"), &dialog);
    layout->addWidget(closeButton);
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

void OnlineWindow::showTargetInviteDialog(const QJsonArray& players) {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("邀请指定玩家"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(QStringLiteral("只显示已登录、处于大厅且愿意接收邀请的玩家"), &dialog));
    auto* list = new QListWidget(&dialog);
    for (const QJsonValue& value : players) {
        const QJsonObject player = value.toObject();
        auto* item = new QListWidgetItem(QStringLiteral("%1，编号 %2")
            .arg(player.value(QStringLiteral("nickname")).toString(),
                 player.value(QStringLiteral("short_id")).toString()), list);
        item->setData(Qt::UserRole, player.value(QStringLiteral("account")).toString());
    }
    list->setAccessibleName(QStringLiteral("可邀请的大厅玩家"));
    layout->addWidget(list);
    auto* button = makeButton(QStringLiteral("发送定向邀请"), &dialog);
    button->setEnabled(list->count() > 0);
    layout->addWidget(button);
    connect(button, &QPushButton::clicked, &dialog, &QDialog::accept);
    if (list->count() > 0) list->setCurrentRow(0);
    if (dialog.exec() == QDialog::Accepted && list->currentItem())
        send(QStringLiteral("invite"), {{QStringLiteral("scope"), QStringLiteral("target")},
            {QStringLiteral("account"), list->currentItem()->data(Qt::UserRole).toString()}}, true);
}

} // namespace fpdz
