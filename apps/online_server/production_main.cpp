#include "core/engine/game_engine.h"

#include "ai/standard_ai.h"

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
#include <QTextStream>
#include <QScopeGuard>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QSet>
#include <QTimer>
#include <QUuid>
#include <QWebSocket>
#include <QWebSocketServer>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <chrono>
#include <future>
#include <sodium.h>

using namespace fpdz;

namespace {
constexpr int kVersion = 1;
constexpr int kMaxMessageBytes = 16 * 1024;
QHash<QString, QString> nicknames;
QHash<QString, QString> publicIds;
QHash<QString, QString> accountsByPublicId;
const QString kBotPrefix = QStringLiteral("@bot:");

bool isBotSeat(const QString& seat) { return seat.startsWith(kBotPrefix); }

QString botSeat(const QString& roomId, int number) {
    return kBotPrefix + roomId + QLatin1Char(':') + QString::number(number);
}

QString firstHumanSeat(const QStringList& seats) {
    for (const QString& seat : seats)
        if (!isBotSeat(seat)) return seat;
    return {};
}

struct Client {
    QString account;
    QString nickname;
    QString room;
    QString sessionHash;
    bool acceptsInvites = true;
    qint64 lastMessageMs = 0;
    qint64 lastChatMs = 0;
    qint64 lastInviteMs = 0;
    qint64 disconnectedMs = 0;
    int burstCount = 0;
};

struct Room {
    QString id;
    int playerCount = 0;
    QStringList seats;
    QString host;
    QString passwordHash;
    GameEngine engine;
    QString roundId;
    QString status = QStringLiteral("waiting");
    qint64 seq = 0;
    qint64 deadlineMs = 0;
    int turnSeconds = 15;
    bool autoUsed = false;
    bool paused = false;
    QJsonArray recentMessages;
    qint64 lastGlobalInviteMs = 0;
};

struct Invitation {
    QString account;
    QString room;
    qint64 expiresMs = 0;
    QString sender;
};

struct BotTask {
    QString roundId;
    qint64 seq = 0;
    PlayerId actor = PlayerId::Player1;
    std::shared_future<GameCommand> result;
};

QString tokenHash(const QString& token) {
    const QByteArray digest = QCryptographicHash::hash(token.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toHex());
}

bool validAccount(const QString& value) {
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_]{3,24}$"));
    return pattern.match(value).hasMatch();
}

bool validDisplayText(const QString& value, int limit) {
    if (value.isEmpty() || value.toUcs4().size() > limit) return false;
    for (const QChar ch : value)
        if (!ch.isPrint() && !ch.isHighSurrogate() && !ch.isLowSurrogate()) return false;
    return true;
}

QString passwordHash(const QString& password) {
    QByteArray encoded(crypto_pwhash_STRBYTES, '\0');
    if (crypto_pwhash_str_alg(encoded.data(), password.toUtf8().constData(),
                          password.toUtf8().size(),
                          crypto_pwhash_OPSLIMIT_INTERACTIVE,
                          crypto_pwhash_MEMLIMIT_INTERACTIVE,
                          crypto_pwhash_ALG_ARGON2ID13) != 0) return {};
    return QString::fromLatin1(encoded.constData());
}

bool verifyPassword(const QString& hash, const QString& password) {
    return !hash.isEmpty() && crypto_pwhash_str_verify(
        hash.toLatin1().constData(), password.toUtf8().constData(),
        password.toUtf8().size()) == 0;
}

QString newToken() {
    QByteArray bytes(32, '\0');
    randombytes_buf(bytes.data(), bytes.size());
    return QString::fromLatin1(bytes.toHex());
}

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
            {"password_required", !room.passwordHash.isEmpty()},
            {"seq", QString::number(room.seq)}};
}

QJsonObject viewFor(const Room& room, const QString& account) {
    QJsonObject view = roomSummary(room);
    view["round_id"] = room.roundId.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                 : QJsonValue(room.roundId);
    view["turn_seconds"] = room.turnSeconds;
    view["host"] = publicIds.value(room.host);
    view["is_host"] = !account.isEmpty() && room.host == account;
    view["deadline_ms"] = QString::number(room.deadlineMs);
    view["record_eligible"] = !room.autoUsed;
    view["self_seat"] = room.seats.indexOf(account);
    if (room.seats.contains(account)) view["recent_messages"] = room.recentMessages;
    QJsonArray seats;
    for (int i = 0; i < room.playerCount; ++i) {
        const QString username = i < room.seats.size() ? room.seats[i] : QString();
        const QString nickname = isBotSeat(username)
            ? QStringLiteral("机器人 %1").arg(username.section(QLatin1Char(':'), -1))
            : nicknames.value(username);
        QJsonObject seat{{"index", i}, {"nickname", nickname},
                         {"short_id", publicIds.value(username)}};
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
    if (publicState.roundResult.valid) {
        QJsonArray scores;
        for (int i = 0; i < room.playerCount; ++i)
            scores.append(QString::number(publicState.roundResult.scoreChanges[i]));
        view["score_changes"] = scores;
    }
    const int selfSeat = room.seats.indexOf(account);
    view["self_seat"] = selfSeat;
    if (selfSeat >= 0) {
        view["hand"] = cardIds(room.engine.fullState().players[selfSeat].hand.cards());
    }
    return view;
}

class Server final : public QObject {
public:
    Server() : ws_(QStringLiteral("Feichuan online production server"),
                   QWebSocketServer::NonSecureMode) {
        const QString dbHost = qEnvironmentVariable("FPDZ_DB_HOST", "127.0.0.1");
        db_ = QSqlDatabase::addDatabase(QStringLiteral("QPSQL"));
        db_.setHostName(dbHost);
        db_.setPort(qEnvironmentVariableIntValue("FPDZ_DB_PORT"));
        if (db_.port() <= 0) db_.setPort(5432);
        db_.setDatabaseName(qEnvironmentVariable("FPDZ_DB_NAME", "fpdz_online"));
        db_.setUserName(qEnvironmentVariable("FPDZ_DB_USER", "fpdz_online"));
        db_.setPassword(qEnvironmentVariable("FPDZ_DB_PASSWORD"));
    }

    bool start() {
        if (sodium_init() < 0) { qCritical("libsodium initialization failed"); return false; }
        if (!db_.open()) { qCritical("database connection failed"); return false; }
        const bool bootstrap = qEnvironmentVariable("FPDZ_ALLOW_BOOTSTRAP") == QStringLiteral("1");
        if (!bootstrap) {
            QSqlQuery migration(db_);
            if (!migration.exec(QStringLiteral("SELECT max(version) FROM online_schema_version")) ||
                !migration.next() || migration.value(0).toInt() != 1) {
                qCritical("online schema migration 1 required before startup");
                return false;
            }
        }
        if (bootstrap) {
        QSqlQuery schema(db_);
        if (!schema.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS online_accounts ("
            "username text PRIMARY KEY, public_id text NOT NULL UNIQUE, "
            "nickname text NOT NULL, password_hash text NOT NULL, "
            "accept_invites boolean NOT NULL DEFAULT true, created_at timestamptz NOT NULL DEFAULT now(), "
            "CHECK (username = lower(username)))"))) {
            qCritical("account schema failed"); return false;
        }
        if (!schema.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS online_sessions ("
            "token_hash text PRIMARY KEY, username text NOT NULL REFERENCES online_accounts(username), "
            "created_at timestamptz NOT NULL DEFAULT now(), revoked_at timestamptz)"))) {
            qCritical("session schema failed"); return false;
        }
        if (!schema.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS online_rooms ("
            "room_id text PRIMARY KEY, player_count integer NOT NULL, "
            "seats jsonb NOT NULL, status text NOT NULL, state jsonb, "
            "round_id text, seq bigint NOT NULL, deadline_ms bigint NOT NULL, "
            "turn_seconds integer NOT NULL, auto_used boolean NOT NULL, "
            "host text, password_hash text)"))) {
            qCritical("room schema failed"); return false;
        }
        if (!schema.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS online_requests ("
            "account text NOT NULL, request_id uuid NOT NULL, response jsonb NOT NULL, "
            "PRIMARY KEY(account, request_id))"))) {
            qCritical("request schema failed"); return false;
        }
        if (!schema.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS online_results ("
            "round_id text PRIMARY KEY, room_id text NOT NULL, player_count integer NOT NULL, "
            "eligible boolean NOT NULL, result jsonb NOT NULL, completed_at timestamptz NOT NULL DEFAULT now())"))) {
            qCritical("result schema failed"); return false;
        }
        if (!schema.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS online_stats ("
            "username text NOT NULL REFERENCES online_accounts(username), player_count integer NOT NULL, "
            "games integer NOT NULL DEFAULT 0, wins integer NOT NULL DEFAULT 0, "
            "PRIMARY KEY(username,player_count))"))) {
            qCritical("stats schema failed"); return false;
        }
        if (!schema.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS online_schema_version ("
            "version integer PRIMARY KEY,applied_at timestamptz NOT NULL DEFAULT now())")) ||
            !schema.exec(QStringLiteral(
            "INSERT INTO online_schema_version(version) VALUES(1) ON CONFLICT DO NOTHING"))) {
            qCritical("bootstrap version failed"); return false;
        }
        }
        QSqlQuery names(db_);
        if (!names.exec(QStringLiteral("SELECT username,nickname,public_id FROM online_accounts"))) {
            qCritical("account loading failed"); return false;
        }
        while (names.next()) {
            const QString username = names.value(0).toString();
            const QString publicId = names.value(2).toString();
            nicknames.insert(username, names.value(1).toString());
            publicIds.insert(username, publicId);
            accountsByPublicId.insert(publicId, username);
        }
        for (int count : {2, 3, 4}) {
            for (int number = 1; number <= 20; ++number) {
                Room room;
                room.id = QStringLiteral("%1-%2").arg(count).arg(number, 2, 10, QLatin1Char('0'));
                room.playerCount = count;
                QSqlQuery insert(db_);
                insert.prepare(QStringLiteral(
                    "INSERT INTO online_rooms(room_id,player_count,seats,status,state,"
                    "round_id,seq,deadline_ms,turn_seconds,auto_used,host,password_hash) "
                    "VALUES(?,?,'[]'::jsonb,'waiting',NULL,NULL,0,0,15,false,NULL,NULL) "
                    "ON CONFLICT(room_id) DO NOTHING"));
                insert.addBindValue(room.id);
                insert.addBindValue(count);
                if (!insert.exec()) { qCritical("room initialization failed"); return false; }
                QSqlQuery load(db_);
                load.prepare(QStringLiteral(
                    "SELECT seats,status,state,round_id,seq,deadline_ms,turn_seconds,auto_used,host,password_hash "
                    "FROM online_rooms WHERE room_id=?"));
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
                room.host = load.value(8).toString();
                room.passwordHash = load.value(9).toString();
                if (room.status == QStringLiteral("finished") && load.value(2).isNull()) {
                    room.status = QStringLiteral("waiting");
                    room.roundId.clear();
                }
                if (room.status == QStringLiteral("playing"))
                    room.deadlineMs = nowMs() + 30000;
                for (const QString& seat : room.seats)
                    if (!isBotSeat(seat)) disconnectedAt_.insert(seat, nowMs());
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
        diagnosticTimer_.setInterval(60000);
        connect(&diagnosticTimer_, &QTimer::timeout, this, [this]() {
            QJsonObject codes;
            for (auto it = errorCodes_.cbegin(); it != errorCodes_.cend(); ++it)
                codes.insert(it.key(), it.value());
            int playing = 0, paused = 0;
            for (const Room& room : rooms_) {
                if (room.status == QStringLiteral("playing")) ++playing;
                if (room.paused) ++paused;
            }
            qInfo().noquote() << QStringLiteral("online_diagnostic ") +
                compact({{"connected", clients_.size()}, {"playing_rooms", playing},
                         {"paused_rooms", paused}, {"error_codes", codes}});
            errorCodes_.clear();
        });
        diagnosticTimer_.start();
        qInfo("online production server ready");
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
        errorCodes_[code]++;
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
                const Client client = clients_.value(socket);
                if (!client.room.isEmpty() && !client.account.isEmpty()) {
                    // A deliberate exit always announces itself first: the online
                    // window sends "leave" before closing and logout revokes the
                    // session. Any other socket end is an unexpected drop, so the
                    // seat is kept for the 30-second reconnect window; the room
                    // timer then plays for the seat and, in a non-playing room,
                    // the seat is released once that window ends.
                    qInfo("client_disconnect close_code=%d seated=1", int(socket->closeCode()));
                    disconnectedAt_.insert(client.account, nowMs());
                }
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

    bool connectedInRoom(const QString& account, const QString& roomId) const {
        QWebSocket* socket = connectedAccount(account);
        return socket && clients_.value(socket).room == roomId;
    }

    bool persist(const Room& candidate, const QString& account,
                 const QString& requestId, const QJsonObject& response) {
        if (!db_.transaction()) return false;
        QJsonArray seats;
        for (const QString& seat : candidate.seats) seats.append(seat);
        QSqlQuery save(db_);
        save.prepare(QStringLiteral(
            "UPDATE online_rooms SET seats=?::jsonb,status=?,state=?::jsonb,"
            "round_id=?,seq=?,deadline_ms=?,turn_seconds=?,auto_used=?,"
            "host=?,password_hash=? WHERE room_id=?"));
        save.addBindValue(QString::fromUtf8(QJsonDocument(seats).toJson(QJsonDocument::Compact)));
        save.addBindValue(candidate.status);
        save.addBindValue(candidate.status != QStringLiteral("playing") ? QStringLiteral("null")
                      : compact(candidate.engine.state().toJson()));
        save.addBindValue(candidate.roundId.isEmpty() ? QVariant()
                      : QVariant(candidate.roundId));
        save.addBindValue(candidate.seq);
        save.addBindValue(candidate.deadlineMs);
        save.addBindValue(candidate.turnSeconds);
        save.addBindValue(candidate.autoUsed);
        save.addBindValue(candidate.host.isEmpty() ? QVariant() : QVariant(candidate.host));
        save.addBindValue(candidate.passwordHash.isEmpty() ? QVariant() : QVariant(candidate.passwordHash));
        save.addBindValue(candidate.id);
        if (!save.exec() || save.numRowsAffected() != 1) { db_.rollback(); return false; }
        if (candidate.status == QStringLiteral("finished") && !candidate.roundId.isEmpty()) {
            const auto result = candidate.engine.publicSnapshot().roundResult;
            if (!result.valid) { db_.rollback(); return false; }
            QJsonArray scores;
            for (int i = 0; i < candidate.playerCount; ++i)
                scores.append(QString::number(result.scoreChanges[i]));
            QSqlQuery finish(db_);
            finish.prepare(QStringLiteral(
                "INSERT INTO online_results(round_id,room_id,player_count,eligible,result) "
                "VALUES(?,?,?,?,?::jsonb) ON CONFLICT(round_id) DO NOTHING"));
            finish.addBindValue(candidate.roundId);
            finish.addBindValue(candidate.id);
            finish.addBindValue(candidate.playerCount);
            finish.addBindValue(!candidate.autoUsed);
            finish.addBindValue(compact({{"score_changes", scores}, {"landlord_won", result.landlordWon}}));
            if (!finish.exec()) { db_.rollback(); return false; }
            if (finish.numRowsAffected() == 1 && !candidate.autoUsed) {
                for (int i = 0; i < candidate.playerCount; ++i) {
                    QSqlQuery stat(db_);
                    stat.prepare(QStringLiteral(
                        "INSERT INTO online_stats(username,player_count,games,wins) VALUES(?,?,1,?) "
                        "ON CONFLICT(username,player_count) DO UPDATE SET "
                        "games=online_stats.games+1,wins=online_stats.wins+EXCLUDED.wins"));
                    stat.addBindValue(candidate.seats[i]);
                    stat.addBindValue(candidate.playerCount);
                    stat.addBindValue(result.scoreChanges[i] > 0 ? 1 : 0);
                    if (!stat.exec()) { db_.rollback(); return false; }
                }
            }
        }
        if (!requestId.isEmpty()) {
            QSqlQuery dedupe(db_);
            dedupe.prepare(QStringLiteral(
                "INSERT INTO online_requests(account,request_id,response) "
                "VALUES(?,?::uuid,?::jsonb)"));
            dedupe.addBindValue(account);
            dedupe.addBindValue(requestId);
            QJsonObject durableResponse = response;
            QJsonObject durablePayload = durableResponse.value("payload").toObject();
            QJsonObject durableView = durablePayload.value("view").toObject();
            durableView.remove("hand");
            durableView.remove("recent_messages");
            durablePayload["view"] = durableView;
            durableResponse["payload"] = durablePayload;
            dedupe.addBindValue(compact(durableResponse));
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
        if (room.status == QStringLiteral("finished")) finishedAt_.insert(room.id, nowMs());
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
        if (nowMs() - client.lastMessageMs > 1000) {
            client.lastMessageMs = nowMs();
            client.burstCount = 0;
        }
        if (++client.burstCount > 30) { socket->close(); return; }
        const QString type = request.value("type").toString();
        const QJsonObject payload = request.value("payload").toObject();
        if (type == QStringLiteral("register") || type == QStringLiteral("login") ||
            type == QStringLiteral("resume")) {
            if (!client.account.isEmpty()) {
                error(socket, requestId, nullptr, QStringLiteral("already_logged_in")); return;
            }
            const QString raw = payload.value("username").toString();
            const QString account = raw.toLower();
            const QString password = payload.value("password").toString();
            const QString token = payload.value("token").toString();
            const QString peerAddress = socket->peerAddress().toString();
            const QString rateKey = peerAddress + QLatin1Char(':') + account;
            const qint64 time = nowMs();
            auto& ipRate = authIpAttempts_[peerAddress];
            if (time - ipRate.second > 60000) ipRate = {0, time};
            if (++ipRate.first > 240) {
                error(socket, requestId, nullptr, QStringLiteral("auth_limited")); return;
            }
            auto& rate = authAttempts_[rateKey];
            if (time - rate.second > 60000) rate = {0, time};
            if (rate.first >= 8) {
                error(socket, requestId, nullptr, QStringLiteral("auth_limited")); return;
            }
            QString nickname;
            bool accepts = true;
            if (type == QStringLiteral("register")) {
                nickname = payload.value("nickname").toString();
                if (!validAccount(raw) || !validDisplayText(nickname, 16) ||
                    password.size() < 10 || password.size() > 128) {
                    error(socket, requestId, nullptr, QStringLiteral("invalid_registration")); return;
                }
                const QString hash = passwordHash(password);
                if (hash.isEmpty()) { error(socket, requestId, nullptr, QStringLiteral("auth_unavailable")); return; }
                if (!db_.transaction()) {
                    error(socket, requestId, nullptr, QStringLiteral("database_unavailable")); return;
                }
                QSqlQuery insert(db_);
                const QString publicId = newToken().left(10);
                insert.prepare(QStringLiteral("INSERT INTO online_accounts(username,public_id,nickname,password_hash) "
                                              "VALUES(?,?,?,?) ON CONFLICT DO NOTHING"));
                insert.addBindValue(account);
                insert.addBindValue(publicId);
                insert.addBindValue(nickname);
                insert.addBindValue(hash);
                if (!insert.exec()) {
                    db_.rollback();
                    error(socket, requestId, nullptr, QStringLiteral("database_unavailable")); return;
                }
                if (insert.numRowsAffected() != 1) {
                    db_.rollback();
                    ++rate.first;
                    error(socket, requestId, nullptr, QStringLiteral("username_taken")); return;
                }
            } else if (type == QStringLiteral("login")) {
                if (!validAccount(raw) || password.size() < 10 || password.size() > 128) {
                    ++rate.first;
                    error(socket, requestId, nullptr, QStringLiteral("invalid_credentials")); return;
                }
                QSqlQuery lookup(db_);
                lookup.prepare(QStringLiteral("SELECT nickname,password_hash,accept_invites "
                                              "FROM online_accounts WHERE username=?"));
                lookup.addBindValue(account);
                if (!lookup.exec()) { error(socket, requestId, nullptr, QStringLiteral("database_unavailable")); return; }
                if (!lookup.next() || !verifyPassword(lookup.value(1).toString(), password)) {
                    ++rate.first;
                    error(socket, requestId, nullptr, QStringLiteral("invalid_credentials")); return;
                }
                nickname = lookup.value(0).toString();
                accepts = lookup.value(2).toBool();
            } else {
                if (token.size() != 64) {
                    ++rate.first;
                    error(socket, requestId, nullptr, QStringLiteral("invalid_session")); return;
                }
                QSqlQuery lookup(db_);
                lookup.prepare(QStringLiteral("SELECT a.username,a.nickname,a.accept_invites "
                    "FROM online_sessions s JOIN online_accounts a ON a.username=s.username "
                    "WHERE s.token_hash=? AND s.revoked_at IS NULL"));
                lookup.addBindValue(tokenHash(token));
                if (!lookup.exec()) { error(socket, requestId, nullptr, QStringLiteral("database_unavailable")); return; }
                if (!lookup.next()) {
                    ++rate.first;
                    error(socket, requestId, nullptr, QStringLiteral("invalid_session")); return;
                }
                nickname = lookup.value(1).toString();
                accepts = lookup.value(2).toBool();
            }
            const QString actualAccount = type == QStringLiteral("resume")
                ? [&]() {
                    QSqlQuery q(db_);
                    q.prepare(QStringLiteral("SELECT username FROM online_sessions WHERE token_hash=?"));
                    q.addBindValue(tokenHash(token));
                    return q.exec() && q.next() ? q.value(0).toString() : QString();
                  }() : account;
            if (actualAccount.isEmpty() || connectedAccount(actualAccount)) {
                if (type == QStringLiteral("register")) db_.rollback();
                error(socket, requestId, nullptr, QStringLiteral("account_connected")); return;
            }
            QString issuedToken;
            if (type != QStringLiteral("resume")) {
                issuedToken = newToken();
                QSqlQuery session(db_);
                session.prepare(QStringLiteral("INSERT INTO online_sessions(token_hash,username) VALUES(?,?)"));
                session.addBindValue(tokenHash(issuedToken));
                session.addBindValue(actualAccount);
                if (!session.exec()) {
                    if (type == QStringLiteral("register")) db_.rollback();
                    error(socket, requestId, nullptr, QStringLiteral("database_unavailable")); return;
                }
            } else issuedToken = token;
            if (type == QStringLiteral("register") && !db_.commit()) {
                db_.rollback();
                error(socket, requestId, nullptr, QStringLiteral("database_unavailable")); return;
            }
            client.account = actualAccount;
            client.nickname = nickname;
            client.acceptsInvites = accepts;
            client.sessionHash = tokenHash(issuedToken);
            disconnectedAt_.remove(actualAccount);
            nicknames.insert(actualAccount, nickname);
            if (type == QStringLiteral("register")) {
                QSqlQuery identity(db_);
                identity.prepare(QStringLiteral("SELECT public_id FROM online_accounts WHERE username=?"));
                identity.addBindValue(actualAccount);
                if (identity.exec() && identity.next()) {
                    const QString publicId = identity.value(0).toString();
                    publicIds.insert(actualAccount, publicId);
                    accountsByPublicId.insert(publicId, actualAccount);
                }
            }
            rate.first = 0;
            for (auto it = rooms_.begin(); it != rooms_.end(); ++it)
                if (it.value().seats.contains(actualAccount)) client.room = it.key();
            QJsonObject details{{"token", issuedToken}, {"username", actualAccount},
                                {"nickname", nickname}, {"room_id", client.room},
                                {"accept_invites", accepts}};
            if (!client.room.isEmpty()) {
                details["view"] = viewFor(rooms_[client.room], actualAccount);
                broadcast(rooms_[client.room]);
            }
            send(socket, envelope(QStringLiteral("ok"), requestId, nullptr, details));
            return;
        }
        if (client.account.isEmpty()) {
            qWarning("unauthenticated_type=%s", qPrintable(type));
            error(socket, requestId, nullptr, QStringLiteral("login_required")); return;
        }
        if (type == QStringLiteral("logout")) {
            QSqlQuery revoke(db_);
            revoke.prepare(QStringLiteral("UPDATE online_sessions SET revoked_at=now() "
                                          "WHERE token_hash=? AND revoked_at IS NULL"));
            revoke.addBindValue(client.sessionHash);
            if (!revoke.exec()) { error(socket, requestId, nullptr, QStringLiteral("database_unavailable")); return; }
            if (!client.room.isEmpty()) {
                exitAfterRound_.insert(client.account);
                disconnectedAt_.insert(client.account, nowMs() - 30000);
            }
            client.account.clear();
            client.room.clear();
            client.sessionHash.clear();
            send(socket, envelope(QStringLiteral("ok"), requestId, nullptr, QJsonObject{}));
            return;
        }
        if (type == QStringLiteral("set_invite_accept")) {
            const bool enabled = payload.value("enabled").toBool();
            QSqlQuery update(db_);
            update.prepare(QStringLiteral("UPDATE online_accounts SET accept_invites=? WHERE username=?"));
            update.addBindValue(enabled);
            update.addBindValue(client.account);
            if (!update.exec()) { error(socket, requestId, nullptr, QStringLiteral("database_unavailable")); return; }
            client.acceptsInvites = enabled;
            send(socket, envelope(QStringLiteral("ok"), requestId, nullptr, {{"enabled", enabled}}));
            return;
        }
        if (type == QStringLiteral("list_players")) {
            QJsonArray players;
            for (auto it = clients_.cbegin(); it != clients_.cend(); ++it) {
                const Client& peer = it.value();
                if (!peer.account.isEmpty() && peer.room.isEmpty() && peer.acceptsInvites &&
                    peer.account != client.account)
                    players.append(QJsonObject{{"account", publicIds.value(peer.account)},
                        {"nickname", peer.nickname}, {"short_id", publicIds.value(peer.account)}});
            }
            send(socket, envelope(QStringLiteral("ok"), requestId, nullptr, {{"players", players}}));
            return;
        }
        if (type == QStringLiteral("diagnostics")) {
            QJsonObject diagnostic{{"server_time_ms", QString::number(nowMs())},
                {"version", kVersion}, {"connected_sockets", clients_.size()},
                {"database_open", db_.isOpen()}, {"room_id", client.room}};
            if (!client.room.isEmpty()) {
                const Room& own = rooms_[client.room];
                diagnostic["room_status"] = own.status;
                diagnostic["round_id"] = own.roundId;
                diagnostic["seq"] = QString::number(own.seq);
                diagnostic["room_paused"] = own.paused;
            }
            send(socket, envelope(QStringLiteral("ok"), requestId, nullptr,
                                  {{"diagnostics", diagnostic}}));
            return;
        }
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
            "SELECT response FROM online_requests WHERE account=? AND request_id=?::uuid"));
        previous.addBindValue(client.account);
        previous.addBindValue(requestId);
        if (!previous.exec()) { room.paused = true; error(socket, requestId, &room, QStringLiteral("database_unavailable")); return; }
        if (previous.next()) {
            QJsonObject replay = QJsonDocument::fromJson(previous.value(0).toByteArray()).object();
            QJsonObject replayPayload = replay.value("payload").toObject();
            replayPayload["view"] = viewFor(room, client.account);
            replay["payload"] = replayPayload;
            replay["round_id"] = room.roundId.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                   : QJsonValue(room.roundId);
            replay["seq"] = QString::number(room.seq);
            send(socket, replay);
            return;
        }
        if (type == QStringLiteral("resync")) {
            if (client.room != room.id || !room.seats.contains(client.account)) {
                error(socket, requestId, &room, QStringLiteral("not_seated")); return;
            }
            send(socket, envelope(QStringLiteral("ok"), requestId, &room,
                                  {{"view", viewFor(room, client.account)},
                                   {"recent_messages", room.recentMessages}}));
            return;
        }
        if (type == QStringLiteral("join") && !room.seats.contains(client.account) &&
            (room.status != QStringLiteral("waiting") || room.seats.size() >= room.playerCount)) {
            error(socket, requestId, &room, QStringLiteral("room_full")); return;
        }
        const bool changesRoom = type == QStringLiteral("join") || type == QStringLiteral("leave") ||
            type == QStringLiteral("set_password") || type == QStringLiteral("set_turn_seconds") ||
            type == QStringLiteral("invite_bots") ||
            type == QStringLiteral("start") || type == QStringLiteral("bid") ||
            type == QStringLiteral("play") || type == QStringLiteral("pass");
        if (changesRoom && request.value("seq").toString() != QString::number(room.seq)) {
            error(socket, requestId, &room, QStringLiteral("stale_seq")); return;
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
            bool invitationValid = false;
            if (!invitationId.isEmpty()) {
                const auto invite = invitations_.find(invitationId);
                if (invite == invitations_.end() || invite->account != client.account ||
                    invite->room != room.id || invite->expiresMs < nowMs()) {
                    error(socket, requestId, &room, QStringLiteral("invitation_expired")); return;
                }
                invitationValid = true;
            }
            if (!room.passwordHash.isEmpty() && !invitationValid) {
                auto& rate = passwordAttempts_[client.account + QLatin1Char(':') + room.id];
                if (nowMs() - rate.second > 60000) rate = {0, nowMs()};
                if (rate.first >= 8) {
                    error(socket, requestId, &room, QStringLiteral("password_limited")); return;
                }
                if (!verifyPassword(room.passwordHash, payload.value("password").toString())) {
                    ++rate.first;
                    error(socket, requestId, &room, QStringLiteral("wrong_password")); return;
                }
                rate.first = 0;
            }
            Room next = room;
            next.seats.append(client.account);
            if (next.host.isEmpty()) next.host = client.account;
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
            Room next = room;
            if (room.status == QStringLiteral("playing")) {
                next.autoUsed = true;
                exitAfterRound_.insert(client.account);
                disconnectedAt_.insert(client.account, nowMs() - 30000);
                if (next.host == client.account) {
                    next.host.clear();
                    for (const QString& seat : next.seats) {
                        if (seat != client.account && connectedInRoom(seat, room.id)) {
                            next.host = seat;
                            break;
                        }
                    }
                }
            } else {
                next.seats.removeAll(client.account);
                if (next.host == client.account || next.host.isEmpty())
                    next.host = firstHumanSeat(next.seats);
                if (next.status == QStringLiteral("finished")) {
                    next.status = QStringLiteral("waiting");
                    next.roundId.clear(); next.engine = GameEngine();
                }
                if (next.host.isEmpty()) {
                    next.seats.clear();
                    next.recentMessages = QJsonArray();
                    next.passwordHash.clear();
                    next.turnSeconds = 15;
                    next.status = QStringLiteral("waiting");
                    next.roundId.clear();
                    next.engine = GameEngine();
                }
            }
            next.seq++;
            commitRoom(socket, requestId, room, std::move(next), client.account);
            if (!room.paused) client.room.clear();
            return;
        }
        if (type == QStringLiteral("invite_bots")) {
            if (room.host != client.account) {
                error(socket, requestId, &room, QStringLiteral("host_required")); return;
            }
            if (room.status != QStringLiteral("waiting") ||
                room.seats.size() >= room.playerCount) {
                error(socket, requestId, &room, QStringLiteral("room_full")); return;
            }
            Room next = room;
            int number = 1;
            for (const QString& seat : next.seats)
                if (isBotSeat(seat)) ++number;
            while (next.seats.size() < next.playerCount)
                next.seats.append(botSeat(next.id, number++));
            next.seq++;
            commitRoom(socket, requestId, room, std::move(next), client.account);
            return;
        }
        if (type == QStringLiteral("set_password")) {
            if (room.host != client.account || room.status != QStringLiteral("waiting") ||
                room.seats.size() != 1) {
                error(socket, requestId, &room, QStringLiteral("host_alone_required")); return;
            }
            const QString password = payload.value("password").toString();
            if (!password.isEmpty() && (password.size() < 10 || password.size() > 128)) {
                error(socket, requestId, &room, QStringLiteral("invalid_password")); return;
            }
            Room next = room;
            next.passwordHash = password.isEmpty() ? QString() : passwordHash(password);
            if (!password.isEmpty() && next.passwordHash.isEmpty()) {
                error(socket, requestId, &room, QStringLiteral("auth_unavailable")); return;
            }
            next.seq++;
            commitRoom(socket, requestId, room, std::move(next), client.account);
            return;
        }
        if (type == QStringLiteral("set_turn_seconds")) {
            if (room.host != client.account || room.status != QStringLiteral("waiting")) {
                error(socket, requestId, &room, QStringLiteral("host_required")); return;
            }
            const int seconds = payload.value("seconds").toInt();
            if (seconds != 15 && seconds != 30 && seconds != 60 && seconds != 120) {
                error(socket, requestId, &room, QStringLiteral("invalid_turn_seconds")); return;
            }
            Room next = room;
            next.turnSeconds = seconds;
            next.seq++;
            commitRoom(socket, requestId, room, std::move(next), client.account);
            return;
        }
        if (type == QStringLiteral("invite")) {
            if (room.status != QStringLiteral("waiting") || room.seats.size() >= room.playerCount) {
                error(socket, requestId, &room, QStringLiteral("room_full")); return;
            }
            const qint64 time = nowMs();
            const bool global = payload.value("scope").toString() == QStringLiteral("all");
            if (global && time - room.lastGlobalInviteMs < 60000) {
                error(socket, requestId, &room, QStringLiteral("invite_limited")); return;
            }
            if (time - lastInviteAccountMs_.value(client.account, 0) < 3000) {
                error(socket, requestId, &room, QStringLiteral("invite_limited")); return;
            }
            const QString targetId = payload.value("account").toString();
            const QString target = accountsByPublicId.value(targetId);
            if (!global && (target.isEmpty() || time - inviteTargetMs_.value(
                    client.account + QLatin1Char(':') + target, 0) < 30000)) {
                error(socket, requestId, &room, QStringLiteral("invite_limited")); return;
            }
            int sent = 0;
            for (auto it = clients_.begin(); it != clients_.end(); ++it) {
                Client& peer = it.value();
                if (peer.account.isEmpty() || !peer.room.isEmpty() || !peer.acceptsInvites ||
                    peer.account == client.account || (!global && peer.account != target)) continue;
                const QString invitationId = newToken();
                invitations_.insert(invitationId, {peer.account, room.id, time + 60000, client.account});
                send(it.key(), envelope(QStringLiteral("invite"), QString(), &room,
                    {{"invitation_id", invitationId}, {"room_id", room.id},
                     {"from_nickname", client.nickname}, {"seq", QString::number(room.seq)},
                     {"expires_ms", QString::number(time + 60000)}}));
                ++sent;
            }
            if (!global && sent == 0) {
                error(socket, requestId, &room, QStringLiteral("recipient_unavailable")); return;
            }
            client.lastInviteMs = time;
            lastInviteAccountMs_.insert(client.account, time);
            if (global) room.lastGlobalInviteMs = time;
            else inviteTargetMs_.insert(client.account + QLatin1Char(':') + target, time);
            send(socket, envelope(QStringLiteral("ok"), requestId, &room, {{"sent", sent}}));
            return;
        }
        if (type == QStringLiteral("chat")) {
            const QString body = payload.value("text").toString();
            if (body.isEmpty() || body.toUcs4().size() > 200 ||
                nowMs() - lastTextMs_.value(client.account, 0) < 1000) {
                error(socket, requestId, &room, QStringLiteral("chat_limited")); return;
            }
            for (const QChar c : body) {
                if (!c.isPrint() && !c.isHighSurrogate() && !c.isLowSurrogate() &&
                    c != QLatin1Char('\n')) {
                    error(socket, requestId, &room, QStringLiteral("invalid_text")); return;
                }
            }
            client.lastChatMs = nowMs();
            lastTextMs_.insert(client.account, nowMs());
            const QJsonObject message{{"nickname", client.nickname}, {"sender", publicIds.value(client.account)},
                                      {"text", body}, {"time_ms", QString::number(nowMs())}};
            room.recentMessages.append(message);
            while (room.recentMessages.size() > 20) room.recentMessages.removeFirst();
            for (auto it = clients_.begin(); it != clients_.end(); ++it)
                if (it.value().room == room.id)
                    send(it.key(), envelope(QStringLiteral("chat"), QString(), &room, message));
            send(socket, envelope(QStringLiteral("ok"), requestId, &room, QJsonObject{}));
            return;
        }
        if (type == QStringLiteral("emote")) {
            const int id = payload.value("id").toInt();
            if (id < 1 || id > 8 || nowMs() - lastTextMs_.value(client.account, 0) < 1000) {
                error(socket, requestId, &room, QStringLiteral("emote_limited")); return;
            }
            client.lastChatMs = nowMs();
            lastTextMs_.insert(client.account, nowMs());
            const QJsonObject event{{"id", id}, {"nickname", client.nickname},
                                    {"sender", publicIds.value(client.account)}};
            for (auto it = clients_.begin(); it != clients_.end(); ++it)
                if (it.value().room == room.id)
                    send(it.key(), envelope(QStringLiteral("emote"), QString(), &room, event));
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
            if (room.seats.size() != room.playerCount || room.host != client.account) {
                error(socket, requestId, &room, QStringLiteral("host_or_seats_required")); return;
            }
            GameCommand command;
            command.type = GameCommandType::StartGame;
            command.playerCount = room.playerCount;
            const CommandResult result = next.engine.execute(command);
            if (!result.success) { error(socket, requestId, &room, QStringLiteral("engine_rejected")); return; }
            next.roundId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            next.status = QStringLiteral("playing");
            next.autoUsed = std::any_of(next.seats.cbegin(), next.seats.cend(), isBotSeat);
        } else {
            if (room.status != QStringLiteral("playing")) {
                error(socket, requestId, &room, QStringLiteral("not_playing")); return;
            }
            const int actor = int(room.engine.fullState().currentPlayer);
            if (actor >= room.seats.size() || room.seats[actor] != client.account) {
                error(socket, requestId, &room, QStringLiteral("not_actor")); return;
            }
            if (botTasks_.contains(room.id)) {
                error(socket, requestId, &room, QStringLiteral("bot_deciding")); return;
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

    bool automaticStep(Room& room, GameCommand command, qint64 time) {
        Room next = room;
        CommandResult result = next.engine.execute(command);
        if (!result.success && command.type == GameCommandType::PlayCards) {
            // A stale or failed worker choice can never become an illegal room move.
            const auto& hand = next.engine.fullState().players[int(command.playerId)].hand.cards();
            if (!hand.empty()) {
                command.cardIds = {hand.front().id()};
                result = next.engine.execute(command);
            }
        }
        if (!result.success) { room.paused = true; qWarning("automatic_step_rejected"); return false; }
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
            room.paused = true; qWarning("automatic_step_commit_failed"); return false;
        }
        room = std::move(next);
        if (room.status == QStringLiteral("finished")) finishedAt_.insert(room.id, time);
        broadcast(room);
        return true;
    }

    void tick() {
        const qint64 time = nowMs();
        if (time - lastPruneMs_ >= 60000) {
            lastPruneMs_ = time;
            for (auto invite = invitations_.begin(); invite != invitations_.end(); )
                invite = invite->expiresMs <= time ? invitations_.erase(invite) : ++invite;
            for (auto attempt = authAttempts_.begin(); attempt != authAttempts_.end(); )
                attempt = time - attempt->second > 60000 ? authAttempts_.erase(attempt) : ++attempt;
            for (auto attempt = authIpAttempts_.begin(); attempt != authIpAttempts_.end(); )
                attempt = time - attempt->second > 60000 ? authIpAttempts_.erase(attempt) : ++attempt;
            for (auto attempt = passwordAttempts_.begin(); attempt != passwordAttempts_.end(); )
                attempt = time - attempt->second > 60000 ? passwordAttempts_.erase(attempt) : ++attempt;
            for (auto invite = inviteTargetMs_.begin(); invite != inviteTargetMs_.end(); )
                invite = time - invite.value() > 60000 ? inviteTargetMs_.erase(invite) : ++invite;
            for (auto invite = lastInviteAccountMs_.begin(); invite != lastInviteAccountMs_.end(); )
                invite = time - invite.value() > 60000 ? lastInviteAccountMs_.erase(invite) : ++invite;
            for (auto message = lastTextMs_.begin(); message != lastTextMs_.end(); )
                message = time - message.value() > 60000 ? lastTextMs_.erase(message) : ++message;
        }
        for (auto it = rooms_.begin(); it != rooms_.end(); ++it) {
            Room& room = it.value();
            if (room.paused) continue;
            auto pending = botTasks_.find(room.id);
            if (pending != botTasks_.end()) {
                if (pending->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                    continue;
                const BotTask task = pending.value();
                botTasks_.erase(pending);
                if (room.status != QStringLiteral("playing") || room.roundId != task.roundId ||
                    room.seq != task.seq || room.engine.fullState().currentPlayer != task.actor)
                    continue;
                try { automaticStep(room, task.result.get(), time); }
                catch (...) { room.paused = true; qWarning("bot_worker_failed"); }
                continue;
            }
            if (room.status != QStringLiteral("playing")) {
                if (room.status == QStringLiteral("finished") &&
                    time - finishedAt_.value(room.id, time) < 5000) continue;
                Room next = room;
                bool changed = false;
                for (const QString& seat : room.seats) {
                    if (!isBotSeat(seat) && !connectedInRoom(seat, room.id) &&
                        (exitAfterRound_.contains(seat) ||
                         time - disconnectedAt_.value(seat, time) >= 30000)) {
                        next.seats.removeAll(seat);
                        exitAfterRound_.remove(seat);
                        disconnectedAt_.remove(seat);
                        changed = true;
                    }
                }
                if (changed) {
                    if (firstHumanSeat(next.seats).isEmpty()) next.seats.clear();
                    if (!next.seats.contains(next.host)) {
                        next.host.clear();
                        for (const QString& seat : next.seats)
                            if (connectedInRoom(seat, room.id)) { next.host = seat; break; }
                    }
                    next.status = QStringLiteral("waiting");
                    next.roundId.clear(); next.engine = GameEngine(); next.deadlineMs = 0;
                    if (next.seats.isEmpty()) {
                        next.passwordHash.clear(); next.turnSeconds = 15;
                        next.recentMessages = QJsonArray(); next.host.clear();
                    }
                    next.seq++;
                    if (persist(next, QString(), QString(), QJsonObject{})) {
                        room = std::move(next); broadcast(room);
                    } else room.paused = true;
                }
                continue;
            }
            bool anyOnline = false;
            qint64 lastDisconnect = 0;
            for (const QString& seat : room.seats) {
                if (isBotSeat(seat)) continue;
                if (connectedInRoom(seat, room.id)) anyOnline = true;
                else lastDisconnect = std::max(lastDisconnect, disconnectedAt_.value(seat, time));
            }
            if (!anyOnline) {
                if (time - lastDisconnect < 30000) continue;
                Room next = room;
                next.seats.clear(); next.host.clear(); next.passwordHash.clear();
                next.turnSeconds = 15; next.recentMessages = QJsonArray();
                next.status = QStringLiteral("waiting"); next.roundId.clear();
                next.engine = GameEngine(); next.deadlineMs = 0; next.seq++;
                if (persist(next, QString(), QString(), QJsonObject{})) {
                    for (const QString& seat : room.seats) {
                        disconnectedAt_.remove(seat); exitAfterRound_.remove(seat);
                    }
                    room = std::move(next);
                } else room.paused = true;
                continue;
            }
            const PlayerId actor = room.engine.fullState().currentPlayer;
            const QString account = room.seats.value(int(actor));
            const bool invitedBot = isBotSeat(account);
            if (!invitedBot && room.deadlineMs > time) continue;
            if (invitedBot) {
                if (botTasks_.size() >= 4) continue;
                const GamePhase phase = room.engine.state().phase();
                if (phase != GamePhase::Bidding && phase != GamePhase::Playing) {
                    room.paused = true; qWarning("bot_invalid_phase"); continue;
                }
                AiObservation observation = makeAiObservation(room.engine.state(), actor);
                if (!observation.fullInformation.available) {
                    room.paused = true; qWarning("invited_bot_observation_invalid"); continue;
                }
                BotTask task;
                task.roundId = room.roundId;
                task.seq = room.seq;
                task.actor = actor;
                task.result = std::async(std::launch::async, [observation]() mutable {
                    StandardAiPlayer ai(AiDifficulty::Beginner, AiInformationMode::FullInformation);
                    return observation.phase == GamePhase::Bidding
                        ? ai.decideBid(observation) : ai.decidePlay(observation);
                }).share();
                botTasks_.insert(room.id, std::move(task));
                continue;
            }
            if (!connectedInRoom(account, room.id) &&
                time - disconnectedAt_.value(account, time) < 30000) continue;
            GameCommand command;
            command.playerId = actor;
            if (room.engine.state().phase() == GamePhase::Bidding) {
                command.type = GameCommandType::Bid;
                command.bidValue = 0;
                automaticStep(room, command, time);
            } else if (room.engine.state().phase() == GamePhase::Playing) {
                const bool mustLead = room.engine.fullState().lastPlayedCards.empty() ||
                    room.engine.fullState().lastPlayedBy == actor;
                if (!mustLead) {
                    command.type = GameCommandType::Pass;
                    automaticStep(room, command, time);
                } else if (botTasks_.size() < 4) {
                    AiObservation observation;
                    observation.playerId = actor;
                    observation.phase = room.engine.state().phase();
                    observation.ownHand = room.engine.fullState().players[int(actor)].hand;
                    observation.publicState = room.engine.publicSnapshot();
                    observation.highestBid = room.engine.fullState().highestBid;
                    observation.decisionSeed = room.engine.state().gameId() * 1000003ULL +
                        static_cast<uint64_t>(room.seq);
                    observation.fullInformation.available = false;
                    BotTask task;
                    task.roundId = room.roundId;
                    task.seq = room.seq;
                    task.actor = actor;
                    task.result = std::async(std::launch::async, [observation]() mutable {
                        StandardAiPlayer ai(AiDifficulty::Beginner, AiInformationMode::PublicInference);
                        return ai.decidePlay(observation);
                    }).share();
                    botTasks_.insert(room.id, std::move(task));
                }
            } else { room.paused = true; qWarning("automatic_step_invalid_phase"); }
        }
    }

    QWebSocketServer ws_;
    QSqlDatabase db_;
    QTimer timer_;
    QTimer diagnosticTimer_;
    QHash<QString, Room> rooms_;
    QHash<QWebSocket*, Client> clients_;
    QHash<QString, Invitation> invitations_;
    QHash<QString, QPair<int, qint64>> authAttempts_;
    QHash<QString, QPair<int, qint64>> authIpAttempts_;
    QHash<QString, QPair<int, qint64>> passwordAttempts_;
    QHash<QString, qint64> inviteTargetMs_;
    QHash<QString, qint64> lastInviteAccountMs_;
    QHash<QString, qint64> lastTextMs_;
    QHash<QString, qint64> disconnectedAt_;
    QSet<QString> exitAfterRound_;
    QHash<QString, BotTask> botTasks_;
    QHash<QString, qint64> finishedAt_;
    QHash<QString, int> errorCodes_;
    qint64 lastPruneMs_ = 0;
    QVector<qint64> processingUs_;
};
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--diagnostics"))) {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QPSQL"),
                                                     QStringLiteral("operator_diagnostics"));
        db.setHostName(qEnvironmentVariable("FPDZ_DB_HOST", "127.0.0.1"));
        db.setPort(qEnvironmentVariableIntValue("FPDZ_DB_PORT") > 0
            ? qEnvironmentVariableIntValue("FPDZ_DB_PORT") : 5432);
        db.setDatabaseName(qEnvironmentVariable("FPDZ_DB_NAME", "fpdz_online"));
        db.setUserName(qEnvironmentVariable("FPDZ_DB_USER", "fpdz_online"));
        db.setPassword(qEnvironmentVariable("FPDZ_DB_PASSWORD"));
        QJsonObject report{{"build_version", QStringLiteral("2.4")},
                           {"database_open", db.open()}};
        if (db.isOpen()) {
            QSqlQuery rooms(db);
            QJsonArray summaries;
            if (rooms.exec(QStringLiteral(
                "SELECT room_id,status,seq FROM online_rooms ORDER BY room_id"))) {
                while (rooms.next()) summaries.append(QJsonObject{
                    {"room_id", rooms.value(0).toString()}, {"status", rooms.value(1).toString()},
                    {"seq", rooms.value(2).toString()}});
            }
            report["rooms"] = summaries;
            report["schema_ok"] = summaries.size() == 60;
            QSqlQuery counts(db);
            if (counts.exec(QStringLiteral("SELECT count(*) FROM online_accounts")) && counts.next())
                report["account_count"] = counts.value(0).toInt();
            if (counts.exec(QStringLiteral("SELECT count(*) FROM online_sessions WHERE revoked_at IS NULL")) && counts.next())
                report["active_session_count"] = counts.value(0).toInt();
            if (counts.exec(QStringLiteral("SELECT count(*) FROM online_results")) && counts.next())
                report["completed_round_count"] = counts.value(0).toInt();
        }
        QTextStream(stdout) << compact(report) << Qt::endl;
        return db.isOpen() && report.value("schema_ok").toBool() ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    Server server;
    if (!server.start()) return EXIT_FAILURE;
    return app.exec();
}
