#include "core/engine/game_engine.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QScopeGuard>
#include <QTimer>
#include <QUuid>
#include <QWebSocket>
#include <QWebSocketServer>

#include <algorithm>
#include <array>
#include <cstdlib>

using namespace fpdz;

namespace {
constexpr int kVersion = 1;
constexpr int kMaxMessageBytes = 16 * 1024;

struct Client {
    QString account;
    QString room;
    qint64 lastMessageMs = 0;
    qint64 lastChatMs = 0;
};

struct Room {
    QString id;
    int playerCount = 0;
    QStringList seats;
    GameEngine engine;
    QString roundId;
    QString status = QStringLiteral("waiting");
    qint64 seq = 0;
    qint64 deadlineMs = 0;
    int turnSeconds = 15;
    bool autoUsed = false;
    bool paused = false;
    QJsonArray recentMessages;
};

struct Invitation {
    QString account;
    QString room;
    qint64 expiresMs = 0;
};

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

QString compact(const QJsonObject& object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QJsonArray cardIds(const std::vector<Card>& cards) {
    QJsonArray values;
    for (const Card& card : cards) values.append(int(card.id()));
    return values;
}

QJsonObject envelope(const QString& type, const QString& requestId,
                     const Room* room, const QJsonObject& payload) {
    QJsonObject object;
    object["version"] = kVersion;
    object["type"] = type;
    object["request_id"] = requestId;
    object["room_id"] = room ? QJsonValue(room->id) : QJsonValue(QJsonValue::Null);
    object["round_id"] = room && !room->roundId.isEmpty()
        ? QJsonValue(room->roundId) : QJsonValue(QJsonValue::Null);
    object["seq"] = room ? QJsonValue(QString::number(room->seq))
                          : QJsonValue(QJsonValue::Null);
    object["payload"] = payload;
    return object;
}

QJsonObject roomSummary(const Room& room) {
    return {{"room_id", room.id}, {"player_count", room.playerCount},
            {"occupied", room.seats.size()}, {"status", room.status},
            {"password_required", false}};
}

QJsonObject viewFor(const Room& room, const QString& account) {
    QJsonObject view = roomSummary(room);
    view["turn_seconds"] = room.turnSeconds;
    view["deadline_ms"] = QString::number(room.deadlineMs);
    view["record_eligible"] = !room.autoUsed;
    QJsonArray seats;
    for (int i = 0; i < room.playerCount; ++i) {
        QJsonObject seat{{"index", i}, {"nickname", i < room.seats.size()
            ? room.seats[i] : QString()}};
        if (room.status != QStringLiteral("waiting") && i < room.seats.size()) {
            const auto& state = room.engine.publicSnapshot().players[i];
            seat["remaining"] = int(state.remainingCards);
            seat["role"] = int(state.role);
            seat["bid"] = state.bidScore;
        }
        seats.append(seat);
    }
    view["seats"] = seats;
    if (room.status == QStringLiteral("waiting")) return view;

    const PublicGameSnapshot publicState = room.engine.publicSnapshot();
    view["phase"] = int(publicState.phase);
    view["current_player"] = int(publicState.currentPlayer);
    view["base_score"] = publicState.baseScore;
    view["multiplier"] = QString::number(publicState.currentMultiplier);
    view["bottom_revealed"] = publicState.bottomCardsRevealed;
    if (publicState.bottomCardsRevealed) view["bottom_cards"] = cardIds(publicState.bottomCards);
    view["last_played_by"] = int(publicState.lastPlayedBy);
    view["last_played_cards"] = cardIds(publicState.lastPlayedCards);
    view["winner"] = publicState.roundResult.valid
        ? QJsonValue(int(publicState.roundResult.winner)) : QJsonValue(QJsonValue::Null);
    const int selfSeat = room.seats.indexOf(account);
    view["self_seat"] = selfSeat;
    if (selfSeat >= 0) {
        view["hand"] = cardIds(room.engine.fullState().players[selfSeat].hand.cards());
    }
    return view;
}

class Server final : public QObject {
public:
    Server() : ws_(QStringLiteral("Feichuan online capacity prototype"),
                   QWebSocketServer::NonSecureMode) {
        const QString dbHost = qEnvironmentVariable("FPDZ_DB_HOST", "127.0.0.1");
        db_ = QSqlDatabase::addDatabase(QStringLiteral("QPSQL"));
        db_.setHostName(dbHost);
        db_.setPort(qEnvironmentVariableIntValue("FPDZ_DB_PORT"));
        if (db_.port() <= 0) db_.setPort(5432);
        db_.setDatabaseName(qEnvironmentVariable("FPDZ_DB_NAME", "fpdz_online_test"));
        db_.setUserName(qEnvironmentVariable("FPDZ_DB_USER", "fpdz_test"));
        db_.setPassword(qEnvironmentVariable("FPDZ_DB_PASSWORD"));
    }

    bool start() {
        if (!db_.open()) { qCritical("database connection failed"); return false; }
        QSqlQuery schema(db_);
        if (!schema.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS prototype_rooms ("
            "room_id text PRIMARY KEY, player_count integer NOT NULL, "
            "seats jsonb NOT NULL, status text NOT NULL, state jsonb, "
            "round_id text, seq bigint NOT NULL, deadline_ms bigint NOT NULL, "
            "turn_seconds integer NOT NULL, auto_used boolean NOT NULL)"))) {
            qCritical("room schema failed"); return false;
        }
        if (!schema.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS prototype_requests ("
            "account text NOT NULL, request_id uuid NOT NULL, response jsonb NOT NULL, "
            "PRIMARY KEY(account, request_id))"))) {
            qCritical("request schema failed"); return false;
        }
        for (int count : {2, 3, 4}) {
            for (int number = 1; number <= 20; ++number) {
                Room room;
                room.id = QStringLiteral("%1-%2").arg(count).arg(number, 2, 10, QLatin1Char('0'));
                room.playerCount = count;
                QSqlQuery insert(db_);
                insert.prepare(QStringLiteral(
                    "INSERT INTO prototype_rooms(room_id,player_count,seats,status,state,"
                    "round_id,seq,deadline_ms,turn_seconds,auto_used) "
                    "VALUES(?,?,'[]'::jsonb,'waiting',NULL,NULL,0,0,15,false) "
                    "ON CONFLICT(room_id) DO NOTHING"));
                insert.addBindValue(room.id);
                insert.addBindValue(count);
                if (!insert.exec()) { qCritical("room initialization failed"); return false; }
                QSqlQuery load(db_);
                load.prepare(QStringLiteral(
                    "SELECT seats,status,state,round_id,seq,deadline_ms,turn_seconds,auto_used "
                    "FROM prototype_rooms WHERE room_id=?"));
                load.addBindValue(room.id);
                if (!load.exec() || !load.next()) { qCritical("room load failed"); return false; }
                const QJsonArray seats = QJsonDocument::fromJson(load.value(0).toByteArray()).array();
                for (const QJsonValue& value : seats) room.seats.append(value.toString());
                room.status = load.value(1).toString();
                if (!load.value(2).isNull()) {
                    const GameState state = GameState::fromJson(
                        QJsonDocument::fromJson(load.value(2).toByteArray()).object());
                    if (!room.engine.restoreState(state)) return false;
                }
                room.roundId = load.value(3).toString();
                room.seq = load.value(4).toLongLong();
                room.deadlineMs = load.value(5).toLongLong();
                room.turnSeconds = load.value(6).toInt();
                room.autoUsed = load.value(7).toBool();
                if (room.status == QStringLiteral("playing"))
                    room.deadlineMs = nowMs() + 30000;
                rooms_.insert(room.id, room);
            }
        }
        const int port = qEnvironmentVariableIntValue("FPDZ_PORT") > 0
            ? qEnvironmentVariableIntValue("FPDZ_PORT") : 19643;
        ws_.setMaxPendingConnections(256);
        if (!ws_.listen(QHostAddress::AnyIPv4, quint16(port))) {
            qCritical("websocket listen failed"); return false;
        }
        connect(&ws_, &QWebSocketServer::newConnection, this, [this]() { accept(); });
        timer_.setInterval(250);
        connect(&timer_, &QTimer::timeout, this, [this]() { tick(); });
        timer_.start();
        qInfo("online capacity prototype ready");
        return true;
    }

private:
    void send(QWebSocket* socket, const QJsonObject& object) {
        if (socket) {
            socket->sendTextMessage(compact(object));
            socket->flush();
        }
    }

    void error(QWebSocket* socket, const QString& requestId, Room* room,
               const QString& code) {
        QJsonObject payload{{"code", code}};
        if (room && clients_.contains(socket))
            payload["view"] = viewFor(*room, clients_[socket].account);
        send(socket, envelope(QStringLiteral("error"), requestId, room, payload));
    }

    void accept() {
        while (ws_.hasPendingConnections()) {
            QWebSocket* socket = ws_.nextPendingConnection();
            clients_.insert(socket, Client{});
            connect(socket, &QWebSocket::textMessageReceived, this,
                    [this, socket](const QString& message) { receive(socket, message); });
            connect(socket, &QWebSocket::disconnected, this, [this, socket]() {
                clients_.remove(socket);
                socket->deleteLater();
            });
        }
    }

    void broadcast(const Room& room) {
        for (auto it = clients_.begin(); it != clients_.end(); ++it) {
            if (it.value().room == room.id) {
                send(it.key(), envelope(QStringLiteral("view"), QString(), &room,
                                        viewFor(room, it.value().account)));
            }
        }
    }

    QWebSocket* connectedAccount(const QString& account) const {
        for (auto it = clients_.cbegin(); it != clients_.cend(); ++it)
            if (it.value().account == account) return it.key();
        return nullptr;
    }

    bool persist(const Room& candidate, const QString& account,
                 const QString& requestId, const QJsonObject& response) {
        if (!db_.transaction()) return false;
        QJsonArray seats;
        for (const QString& seat : candidate.seats) seats.append(seat);
        QSqlQuery save(db_);
        save.prepare(QStringLiteral(
            "UPDATE prototype_rooms SET seats=?::jsonb,status=?,state=?::jsonb,"
            "round_id=?,seq=?,deadline_ms=?,turn_seconds=?,auto_used=? WHERE room_id=?"));
        save.addBindValue(QString::fromUtf8(QJsonDocument(seats).toJson(QJsonDocument::Compact)));
        save.addBindValue(candidate.status);
        save.addBindValue(candidate.status == QStringLiteral("waiting") ? QStringLiteral("null")
                      : compact(candidate.engine.state().toJson()));
        save.addBindValue(candidate.roundId.isEmpty() ? QVariant()
                      : QVariant(candidate.roundId));
        save.addBindValue(candidate.seq);
        save.addBindValue(candidate.deadlineMs);
        save.addBindValue(candidate.turnSeconds);
        save.addBindValue(candidate.autoUsed);
        save.addBindValue(candidate.id);
        if (!save.exec() || save.numRowsAffected() != 1) { db_.rollback(); return false; }
        if (!requestId.isEmpty()) {
            QSqlQuery dedupe(db_);
            dedupe.prepare(QStringLiteral(
                "INSERT INTO prototype_requests(account,request_id,response) "
                "VALUES(?,?::uuid,?::jsonb)"));
            dedupe.addBindValue(account);
            dedupe.addBindValue(requestId);
            dedupe.addBindValue(compact(response));
            if (!dedupe.exec()) { db_.rollback(); return false; }
        }
        if (!db_.commit()) { db_.rollback(); return false; }
        return true;
    }

    void commitRoom(QWebSocket* socket, const QString& requestId, Room& room,
                    Room&& candidate, const QString& account) {
        const QJsonObject result = envelope(QStringLiteral("ok"), requestId, &candidate,
                                             {{"view", viewFor(candidate, account)}});
        if (!persist(candidate, account, requestId, result)) {
            room.paused = true;
            error(socket, requestId, &room, QStringLiteral("database_unavailable"));
            return;
        }
        room = std::move(candidate);
        send(socket, result);
        broadcast(room);
    }

    void receive(QWebSocket* socket, const QString& message) {
        QElapsedTimer elapsed;
        elapsed.start();
        const auto timing = qScopeGuard([this, &elapsed]() {
            processingUs_.push_back(elapsed.nsecsElapsed() / 1000);
            if (processingUs_.size() >= 5000) {
                std::sort(processingUs_.begin(), processingUs_.end());
                const qsizetype index = (processingUs_.size() - 1) * 95 / 100;
                qInfo("server_processing_p95_ms=%.3f samples=%lld",
                      double(processingUs_[index]) / 1000.0,
                      static_cast<long long>(processingUs_.size()));
                processingUs_.clear();
            }
        });
        if (message.toUtf8().size() > kMaxMessageBytes) { socket->close(); return; }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(message.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            error(socket, {}, nullptr, QStringLiteral("invalid_json")); return;
        }
        const QJsonObject request = document.object();
        const QString requestId = request.value("request_id").toString();
        if (request.value("version").toInt() != kVersion) {
            error(socket, requestId, nullptr, QStringLiteral("protocol_version")); return;
        }
        if (QUuid(requestId).isNull()) {
            error(socket, requestId, nullptr, QStringLiteral("invalid_request_id")); return;
        }
        Client& client = clients_[socket];
        const QString type = request.value("type").toString();
        const QJsonObject payload = request.value("payload").toObject();
        if (type == QStringLiteral("test_login")) {
            if (!client.account.isEmpty()) { error(socket, requestId, nullptr, QStringLiteral("already_logged_in")); return; }
            const QString account = payload.value("account").toString();
            if (account.size() < 3 || account.size() > 24) {
                error(socket, requestId, nullptr, QStringLiteral("invalid_account")); return;
            }
            for (const QChar c : account) {
                if (!c.isLetterOrNumber() && c != QLatin1Char('_')) {
                    error(socket, requestId, nullptr, QStringLiteral("invalid_account")); return;
                }
            }
            for (auto it = clients_.begin(); it != clients_.end(); ++it) {
                if (it.key() != socket && it.value().account == account) {
                    error(socket, requestId, nullptr, QStringLiteral("account_connected")); return;
                }
            }
            client.account = account;
            for (auto it = rooms_.begin(); it != rooms_.end(); ++it) {
                if (it.value().seats.contains(account)) client.room = it.key();
            }
            send(socket, envelope(QStringLiteral("ok"), requestId, nullptr,
                                  {{"room_id", client.room}}));
            if (!client.room.isEmpty()) broadcast(rooms_[client.room]);
            return;
        }
        if (client.account.isEmpty()) { error(socket, requestId, nullptr, QStringLiteral("login_required")); return; }
        if (type == QStringLiteral("list")) {
            const int count = payload.value("player_count").toInt();
            QJsonArray list;
            for (auto it = rooms_.cbegin(); it != rooms_.cend(); ++it)
                if (it.value().playerCount == count) list.append(roomSummary(it.value()));
            send(socket, envelope(QStringLiteral("ok"), requestId, nullptr, {{"rooms", list}}));
            return;
        }
        const QString roomId = request.value("room_id").toString();
        auto roomIt = rooms_.find(roomId);
        if (roomIt == rooms_.end()) { error(socket, requestId, nullptr, QStringLiteral("unknown_room")); return; }
        Room& room = roomIt.value();
        if (room.paused) { error(socket, requestId, &room, QStringLiteral("room_paused")); return; }
        QSqlQuery previous(db_);
        previous.prepare(QStringLiteral(
            "SELECT response FROM prototype_requests WHERE account=? AND request_id=?::uuid"));
        previous.addBindValue(client.account);
        previous.addBindValue(requestId);
        if (!previous.exec()) { room.paused = true; error(socket, requestId, &room, QStringLiteral("database_unavailable")); return; }
        if (previous.next()) {
            send(socket, QJsonDocument::fromJson(previous.value(0).toByteArray()).object());
            return;
        }
        if (type == QStringLiteral("resync")) {
            send(socket, envelope(QStringLiteral("ok"), requestId, &room,
                                  {{"view", viewFor(room, client.account)},
                                   {"recent_messages", room.recentMessages}}));
            return;
        }
        if (type == QStringLiteral("join")) {
            if (!client.room.isEmpty() && client.room != room.id) {
                error(socket, requestId, &room, QStringLiteral("already_seated")); return;
            }
            if (room.seats.contains(client.account)) {
                client.room = room.id;
                send(socket, envelope(QStringLiteral("ok"), requestId, &room,
                                      {{"view", viewFor(room, client.account)}}));
                return;
            }
            if (room.status != QStringLiteral("waiting") || room.seats.size() == room.playerCount) {
                error(socket, requestId, &room, QStringLiteral("room_full")); return;
            }
            const QString invitationId = payload.value("invitation_id").toString();
            if (!invitationId.isEmpty()) {
                const auto invite = invitations_.find(invitationId);
                if (invite == invitations_.end() || invite->account != client.account ||
                    invite->room != room.id || invite->expiresMs < nowMs()) {
                    error(socket, requestId, &room, QStringLiteral("invitation_expired")); return;
                }
            }
            Room next = room;
            next.seats.append(client.account);
            next.seq++;
            commitRoom(socket, requestId, room, std::move(next), client.account);
            if (!room.paused) {
                client.room = room.id;
                if (!invitationId.isEmpty()) invitations_.remove(invitationId);
            }
            return;
        }
        if (client.room != room.id || !room.seats.contains(client.account)) {
            error(socket, requestId, &room, QStringLiteral("not_seated")); return;
        }
        if (type == QStringLiteral("leave")) {
            if (room.status != QStringLiteral("waiting")) {
                error(socket, requestId, &room, QStringLiteral("in_round")); return;
            }
            Room next = room;
            next.seats.removeAll(client.account);
            if (next.seats.isEmpty()) next.recentMessages = QJsonArray();
            next.seq++;
            commitRoom(socket, requestId, room, std::move(next), client.account);
            if (!room.paused) client.room.clear();
            return;
        }
        if (type == QStringLiteral("invite")) {
            const QString target = payload.value("account").toString();
            QWebSocket* recipient = connectedAccount(target);
            if (!recipient || !clients_[recipient].room.isEmpty() || target == client.account) {
                error(socket, requestId, &room, QStringLiteral("recipient_unavailable")); return;
            }
            if (room.status != QStringLiteral("waiting") || room.seats.size() >= room.playerCount) {
                error(socket, requestId, &room, QStringLiteral("room_full")); return;
            }
            const QString invitationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            invitations_.insert(invitationId, {target, room.id, nowMs() + 60000});
            send(recipient, envelope(QStringLiteral("invitation"), QString(), &room,
                                     {{"invitation_id", invitationId}, {"sender", client.account},
                                      {"expires_ms", QString::number(nowMs() + 60000)}}));
            send(socket, envelope(QStringLiteral("ok"), requestId, &room, QJsonObject{}));
            return;
        }
        if (type == QStringLiteral("chat")) {
            const QString body = payload.value("text").toString();
            if (body.isEmpty() || body.size() > 200 || nowMs() - client.lastChatMs < 1000) {
                error(socket, requestId, &room, QStringLiteral("chat_limited")); return;
            }
            for (const QChar c : body) {
                if (!c.isPrint() && c != QLatin1Char('\n')) {
                    error(socket, requestId, &room, QStringLiteral("invalid_text")); return;
                }
            }
            client.lastChatMs = nowMs();
            const QJsonObject message{{"sender", client.account}, {"text", body}};
            room.recentMessages.append(message);
            while (room.recentMessages.size() > 20) room.recentMessages.removeFirst();
            for (auto it = clients_.begin(); it != clients_.end(); ++it)
                if (it.value().room == room.id)
                    send(it.key(), envelope(QStringLiteral("chat"), QString(), &room, message));
            send(socket, envelope(QStringLiteral("ok"), requestId, &room, QJsonObject{}));
            return;
        }
        const QString seq = request.value("seq").toString();
        if (seq != QString::number(room.seq)) {
            error(socket, requestId, &room, QStringLiteral("stale_seq")); return;
        }
        if (type != QStringLiteral("start") &&
            request.value("round_id").toString() != room.roundId) {
            error(socket, requestId, &room, QStringLiteral("stale_round")); return;
        }
        Room next = room;
        if (type == QStringLiteral("start")) {
            if (room.status != QStringLiteral("waiting") && room.status != QStringLiteral("finished")) {
                error(socket, requestId, &room, QStringLiteral("already_started")); return;
            }
            if (room.seats.size() != room.playerCount || room.seats.first() != client.account) {
                error(socket, requestId, &room, QStringLiteral("host_or_seats_required")); return;
            }
            GameCommand command;
            command.type = GameCommandType::StartGame;
            command.playerCount = room.playerCount;
            const CommandResult result = next.engine.execute(command);
            if (!result.success) { error(socket, requestId, &room, QStringLiteral("engine_rejected")); return; }
            next.roundId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            next.status = QStringLiteral("playing");
            next.autoUsed = false;
        } else {
            if (room.status != QStringLiteral("playing")) {
                error(socket, requestId, &room, QStringLiteral("not_playing")); return;
            }
            const int actor = int(room.engine.fullState().currentPlayer);
            if (actor >= room.seats.size() || room.seats[actor] != client.account) {
                error(socket, requestId, &room, QStringLiteral("not_actor")); return;
            }
            GameCommand command;
            command.playerId = static_cast<PlayerId>(actor);
            if (type == QStringLiteral("bid")) {
                command.type = GameCommandType::Bid;
                command.bidValue = payload.value("score").toInt(-1);
            } else if (type == QStringLiteral("play")) {
                command.type = GameCommandType::PlayCards;
                const QJsonArray ids = payload.value("card_ids").toArray();
                if (ids.size() > 27) { error(socket, requestId, &room, QStringLiteral("too_many_cards")); return; }
                for (const QJsonValue& id : ids) command.cardIds.push_back(CardId(id.toInt(-1)));
            } else if (type == QStringLiteral("pass")) {
                command.type = GameCommandType::Pass;
            } else {
                error(socket, requestId, &room, QStringLiteral("unknown_type")); return;
            }
            const CommandResult result = next.engine.execute(command);
            if (!result.success) { error(socket, requestId, &room, QStringLiteral("illegal_action")); return; }
            if (next.engine.state().phase() == GamePhase::Finished)
                next.status = QStringLiteral("finished");
        }
        next.seq++;
        next.deadlineMs = next.status == QStringLiteral("playing")
            ? nowMs() + next.turnSeconds * 1000 : 0;
        commitRoom(socket, requestId, room, std::move(next), client.account);
    }

    void tick() {
        const qint64 time = nowMs();
        for (auto it = rooms_.begin(); it != rooms_.end(); ++it) {
            Room& room = it.value();
            if (room.paused || room.status != QStringLiteral("playing") ||
                room.deadlineMs > time) continue;
            Room next = room;
            GameCommand command;
            command.playerId = next.engine.fullState().currentPlayer;
            if (next.engine.state().phase() == GamePhase::Bidding) {
                command.type = GameCommandType::Bid;
                command.bidValue = 0;
            } else if (next.engine.state().phase() == GamePhase::Playing) {
                command.type = GameCommandType::Pass;
                if (next.engine.fullState().lastPlayedCards.empty() ||
                    next.engine.fullState().lastPlayedBy == command.playerId) {
                    command.type = GameCommandType::PlayCards;
                    const auto& hand = next.engine.fullState()
                        .players[int(command.playerId)].hand.cards();
                    if (hand.empty()) { room.paused = true; continue; }
                    command.cardIds.push_back(hand.front().id());
                }
            } else { room.paused = true; continue; }
            const CommandResult result = next.engine.execute(command);
            if (!result.success) { room.paused = true; continue; }
            next.autoUsed = true;
            next.seq++;
            if (next.engine.fullState().consecutiveRedeals >= 3) {
                next.status = QStringLiteral("waiting");
                next.engine = GameEngine();
                next.roundId.clear();
            } else if (next.engine.state().phase() == GamePhase::Finished) {
                next.status = QStringLiteral("finished");
            }
            next.deadlineMs = next.status == QStringLiteral("playing")
                ? time + next.turnSeconds * 1000 : 0;
            if (!persist(next, QString(), QString(), QJsonObject{})) {
                room.paused = true; continue;
            }
            room = std::move(next);
            broadcast(room);
        }
    }

    QWebSocketServer ws_;
    QSqlDatabase db_;
    QTimer timer_;
    QHash<QString, Room> rooms_;
    QHash<QWebSocket*, Client> clients_;
    QHash<QString, Invitation> invitations_;
    QVector<qint64> processingUs_;
};
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    Server server;
    if (!server.start()) return EXIT_FAILURE;
    return app.exec();
}
