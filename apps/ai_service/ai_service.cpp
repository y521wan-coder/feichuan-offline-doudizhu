#include "ai_service.h"
#include "../../src/app/ai_battle_prompt.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslError>
#include <QUrl>
#include <QUrlQuery>

#include <cmath>
#include <limits>

namespace fpdz::ai_service {
namespace {

constexpr int kProtocolVersion = 1;
constexpr qsizetype kMaximumLineBytes = 128 * 1024;
constexpr qsizetype kMaximumResponseBytes = 128 * 1024;

QJsonObject contextFields(const QJsonObject& source) {
    return {{QStringLiteral("protocol_version"), kProtocolVersion},
            {QStringLiteral("type"), QStringLiteral("decision_result")},
            {QStringLiteral("request_id"), source.value("request_id")},
            {QStringLiteral("game_id"), source.value("game_id")},
            {QStringLiteral("event_sequence"), source.value("event_sequence")},
            {QStringLiteral("phase"), source.value("phase")},
            {QStringLiteral("seat"), source.value("seat")}};
}

QJsonObject actionSchema() {
    return {{QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), QJsonObject{
                {QStringLiteral("action_id"), QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("integer")},
                    {QStringLiteral("minimum"), 0}}}}},
            {QStringLiteral("required"), QJsonArray{QStringLiteral("action_id")}},
            {QStringLiteral("additionalProperties"), false}};
}

QString modeRules(int playerCount) {
    switch (playerCount) {
    case 2:
        return QString::fromUtf8(u8"本局二人：单副54张，各17张，3张底牌给地主，另17张盖牌不参与；地主先出，谁先出完谁赢。"
                                 u8"可出单、对、三、三带一或对、顺子、连对、飞机带单或对、四带二单或两对；顺子至少5张、连对至少3对、飞机至少2组三张，主体不含2或王。"
                                 u8"四同炸弹(13)小于一小王加一大王的王炸(14)，二者均压普通牌；一方过牌后上手者重领。叫分最高者当地主，全不叫重发；炸弹、王炸和春天各翻倍。");
    case 3:
        return QString::fromUtf8(u8"本局三人：单副54张，各17张，3张底牌给地主；地主先出，地主出完则胜，任一农民出完则两农民同胜。"
                                 u8"可出单、对、三、三带一或对、顺子、连对、飞机带单或对、四带二单或两对；顺子至少5张、连对至少3对、飞机至少2组三张，主体不含2或王。"
                                 u8"四同炸弹(13)小于一小王加一大王的王炸(14)，二者均压普通牌；其他人都过牌后上手者重领。叫分最高者当地主，全不叫重发；炸弹、王炸和春天各翻倍。");
    case 4:
        return QString::fromUtf8(u8"本局四人：双副108张，各25张，8张底牌给地主后地主33张；地主先出，地主出完则胜，任一农民出完则三农民同胜。"
                                 u8"可出单、对、三、三带一对、顺子(至少5张)、双顺(至少3对)、三顺(至少2组三张)、飞机带同数量且点数互异的对子；主体不含2或王。"
                                 u8"飞机翅膀不能与主体同点：33333 444 55不能作为飞机，333 444 55 66可以；不许三带一、四带二、飞机带单。"
                                 u8"炸弹从小到大：13枪(四同)、14双王枪毙(一小王一大王，枪级最大)、15炮(五同)、16火箭(六同)、17导弹(七同)、18天炸(八同)、19天尊无敌(两小王两大王，天炸级最大)。"
                                 u8"炸弹可压普通牌，同级普通炸弹比点数；其他人都过牌后上手者重领。叫分最高者当地主，全不叫重发；谨慎考虑不同炸弹的倍数代价。");
    default:
        return {};
    }
}

QString promptInstructions(int playerCount, const QString& customStrategy) {
    return QString::fromUtf8(u8"你是全知斗地主决策器，只从legal_actions选择action_id。"
                             u8"player_id、landlord、last_played_by从0起；role=1是地主、2是农民；"
                             u8"all_hands按座位排列。rank从0到14依次是3,4,5,6,7,8,9,10,J,Q,K,A,2,小王,大王；"
                             u8"pattern 1单张、2对子、3三张、4三带一、5三带二、6顺子、7连对、"
                             u8"8飞机、9飞机带单、10飞机带对、11四带二单、12四带两对、13到19炸弹。"
                             u8"地主自己先出完即获胜；任意农民先出完则全体农民获胜，农民应配合队友。")
         + modeRules(playerCount)
         + (customStrategy.trimmed().isEmpty()
                ? defaultAiStrategyPrompt()
                : customStrategy.trimmed().left(AI_MAX_STRATEGY_PROMPT_CHARS))
         + QString::fromUtf8(
               u8"尽快返回最佳合法动作，只能返回单键JSON对象{\"action_id\":7}，"
               u8"其中编号必须来自legal_actions；不要解释、代码块或额外键。");
}

QString safeNetworkMessage(QNetworkReply::NetworkError error, int httpStatus) {
    if (httpStatus == 401) return QString::fromUtf8(u8"认证失败（401）");
    if (httpStatus == 403) return QString::fromUtf8(u8"请求被拒绝（403）");
    if (httpStatus == 404) return QString::fromUtf8(u8"接口或模型不存在（404）");
    if (httpStatus == 429) return QString::fromUtf8(u8"请求过于频繁或额度不足（429）");
    if (httpStatus >= 500) return QString::fromUtf8(u8"模型服务暂时不可用");
    if (error == QNetworkReply::HostNotFoundError) return QString::fromUtf8(u8"无法解析服务地址");
    if (error == QNetworkReply::TimeoutError) return QString::fromUtf8(u8"模型请求超时");
    if (error == QNetworkReply::SslHandshakeFailedError) return QString::fromUtf8(u8"HTTPS证书验证失败");
    return QString::fromUtf8(u8"无法连接模型服务");
}

QString networkErrorCode(QNetworkReply::NetworkError error, int httpStatus) {
    if (httpStatus > 0) return QStringLiteral("http_%1").arg(httpStatus);
    if (error == QNetworkReply::HostNotFoundError) return QStringLiteral("dns_failed");
    if (error == QNetworkReply::TimeoutError) return QStringLiteral("timeout");
    if (error == QNetworkReply::SslHandshakeFailedError) return QStringLiteral("certificate_error");
    return QStringLiteral("connection_failed");
}

// OpenAI-compatible Chat Completions endpoints that follow the DeepSeek naming
// accept none/low/high/max where "none" disables hidden reasoning. The game
// strengths map onto that scale so "快速" really is the lowest-latency choice.
QString compatibleReasoningEffort(const QString& strength) {
    if (strength == QStringLiteral("low")) return QStringLiteral("none");
    if (strength == QStringLiteral("high")) return QStringLiteral("max");
    return QStringLiteral("high");
}

} // namespace

AiService::AiService(QString credentialPath, QObject* parent)
    : QObject(parent), m_credentials(std::move(credentialPath)) {}

void AiService::start() {
    QString error;
    m_storeReady = m_credentials.load(&error);
    QJsonObject ready{{QStringLiteral("protocol_version"), kProtocolVersion},
                      {QStringLiteral("type"), QStringLiteral("ready")},
                      {QStringLiteral("ok"), m_storeReady}};
    if (!m_storeReady) ready[QStringLiteral("message")] = error;
    send(ready);
}

void AiService::handleLine(const QByteArray& line) {
    if (line.size() > kMaximumLineBytes) {
        fail({}, QStringLiteral("message_too_large"), QString::fromUtf8(u8"IPC报文超过128 KiB"));
        return;
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        fail({}, QStringLiteral("invalid_json"), QString::fromUtf8(u8"IPC JSON损坏"));
        return;
    }
    const QJsonObject message = document.object();
    if (message.value("protocol_version").toInt() != kProtocolVersion) {
        fail(message, QStringLiteral("protocol_mismatch"), QString::fromUtf8(u8"AI服务协议版本不匹配"));
        return;
    }
    const QString type = message.value("type").toString();
    if (type == QStringLiteral("decision")) handleDecision(message);
    else if (type == QStringLiteral("test_model")) {
        QJsonObject test = message;
        test[QStringLiteral("type")] = QStringLiteral("decision");
        test[QStringLiteral("game_id")] = 0;
        test[QStringLiteral("event_sequence")] = 0;
        test[QStringLiteral("phase")] = QStringLiteral("playing");
        test[QStringLiteral("seat")] = 1;
        test[QStringLiteral("payload")] = QJsonObject{
            {QStringLiteral("test_only"), true},
            {QStringLiteral("legal_actions"), QJsonArray{
                QJsonObject{{QStringLiteral("action_id"), 0},
                            {QStringLiteral("kind"), QStringLiteral("pass")}}}}};
        handleDecision(test);
    }
    else if (type == QStringLiteral("credentials_list")) handleCredentialList(message);
    else if (type == QStringLiteral("credential_save")) handleCredentialSave(message);
    else if (type == QStringLiteral("credential_delete")) handleCredentialDelete(message);
    else if (type == QStringLiteral("fetch_models")) handleFetchModels(message);
    else if (type == QStringLiteral("cancel")) {
        const QString id = message.value("request_id").toString();
        if (m_pending.contains(id) && m_pending[id].reply) m_pending[id].reply->abort();
    } else {
        fail(message, QStringLiteral("unknown_message"), QString::fromUtf8(u8"未知IPC消息"));
    }
}

void AiService::handleDecision(const QJsonObject& message) {
    if (!m_storeReady) {
        fail(message, QStringLiteral("credentials_unavailable"), QString::fromUtf8(u8"认证仓库不可用"));
        return;
    }
    const QString requestId = message.value("request_id").toString();
    const QJsonObject credential = m_credentials.record(message.value("credential_id").toString());
    if (requestId.isEmpty() || credential.isEmpty() || message.value("model").toString().isEmpty()) {
        fail(message, QStringLiteral("configuration_error"), QString::fromUtf8(u8"云认证或模型未配置"));
        return;
    }
    if (m_pending.contains(requestId)) {
        fail(message, QStringLiteral("duplicate_request"), QString::fromUtf8(u8"请求编号重复"));
        return;
    }
    const int protocol = credential.value("protocol").toInt();
    const QByteArray body = decisionBody(message, credential, protocol);
    if (body.size() > kMaximumLineBytes) {
        fail(message, QStringLiteral("prompt_too_large"), QString::fromUtf8(u8"模型请求超过128 KiB"));
        return;
    }
    startHttpRequest(message, credential, requestUrl(credential, protocol), "POST", body, protocol);
}

void AiService::handleCredentialList(const QJsonObject& message) {
    QJsonObject response{{QStringLiteral("protocol_version"), kProtocolVersion},
                         {QStringLiteral("type"), QStringLiteral("credentials_list_result")},
                         {QStringLiteral("request_id"), message.value("request_id")},
                         {QStringLiteral("ok"), m_storeReady},
                         {QStringLiteral("credentials"), m_credentials.publicRecords()}};
    send(response);
}

void AiService::handleCredentialSave(const QJsonObject& message) {
    if (!m_storeReady) {
        fail(message, QStringLiteral("credentials_unavailable"), QString::fromUtf8(u8"认证仓库不可用"));
        return;
    }
    QString id;
    QString error;
    const bool ok = m_credentials.upsert(message.value("credential").toObject(),
                                         message.value("clear_secret").toBool(), &id, &error);
    QJsonObject response{{QStringLiteral("protocol_version"), kProtocolVersion},
                         {QStringLiteral("type"), QStringLiteral("credential_save_result")},
                         {QStringLiteral("request_id"), message.value("request_id")},
                         {QStringLiteral("ok"), ok},
                         {QStringLiteral("credential_id"), id}};
    if (!ok) response[QStringLiteral("message")] = error;
    send(response);
}

void AiService::handleCredentialDelete(const QJsonObject& message) {
    QString error;
    const bool ok = m_storeReady &&
        m_credentials.remove(message.value("credential_id").toString(), &error);
    QJsonObject response{{QStringLiteral("protocol_version"), kProtocolVersion},
                         {QStringLiteral("type"), QStringLiteral("credential_delete_result")},
                         {QStringLiteral("request_id"), message.value("request_id")},
                         {QStringLiteral("ok"), ok}};
    if (!ok) response[QStringLiteral("message")] = error;
    send(response);
}

void AiService::handleFetchModels(const QJsonObject& message) {
    const QJsonObject credential = m_credentials.record(message.value("credential_id").toString());
    if (credential.isEmpty()) {
        fail(message, QStringLiteral("configuration_error"), QString::fromUtf8(u8"认证不存在"));
        return;
    }
    QJsonObject context = message;
    context[QStringLiteral("type")] = QStringLiteral("fetch_models");
    startHttpRequest(context, credential, modelsUrl(credential), "GET", {}, -1);
}

void AiService::startHttpRequest(const QJsonObject& context, const QJsonObject& credential,
                                 const QUrl& url, const QByteArray& method,
                                 const QByteArray& body, int protocol) {
    if (!url.isValid() || (url.scheme() != QStringLiteral("http") &&
                           url.scheme() != QStringLiteral("https"))) {
        fail(context, QStringLiteral("invalid_url"), QString::fromUtf8(u8"请求地址无效"));
        return;
    }
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    applyAuthentication(request, credential);
    QNetworkReply* reply = method == "GET" ? m_network.get(request) : m_network.post(request, body);
    PendingRequest pending;
    pending.context = context;
    pending.reply = reply;
    pending.protocol = protocol;
    pending.elapsed.start();
    pending.timer = new QTimer(reply);
    pending.timer->setSingleShot(true);
    const int timeout = std::clamp(context.value("timeout_ms").toInt(15000), 3000, 300000);
    const QString requestId = context.value("request_id").toString();
    connect(pending.timer, &QTimer::timeout, reply, [this, requestId, reply]() {
        if (m_pending.contains(requestId)) m_pending[requestId].timedOut = true;
        reply->abort();
    });
    connect(reply, &QNetworkReply::readyRead, this, [this, requestId, reply]() {
        if (!m_pending.contains(requestId)) return;
        auto& item = m_pending[requestId];
        item.response += reply->readAll();
        if (item.response.size() > kMaximumResponseBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, requestId]() { finishHttpRequest(requestId); });
    pending.timer->start(timeout);
    m_pending.insert(requestId, pending);
}

void AiService::finishHttpRequest(const QString& requestId) {
    if (!m_pending.contains(requestId)) return;
    PendingRequest pending = m_pending.take(requestId);
    QNetworkReply* reply = pending.reply;
    if (!reply) return;
    pending.response += reply->readAll();
    if (pending.timer) pending.timer->stop();
    const int latency = static_cast<int>(pending.elapsed.elapsed());
    pending.context[QStringLiteral("latency_ms")] = latency;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QVariant redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
    if (pending.timedOut) {
        fail(pending.context, QStringLiteral("timeout"),
             QString::fromUtf8(u8"模型请求超时"), status);
        reply->deleteLater();
        return;
    }
    if (redirect.isValid()) {
        fail(pending.context, QStringLiteral("redirect_blocked"),
             QString::fromUtf8(u8"模型服务返回重定向；为保护认证信息已阻止自动跟随"), status);
        reply->deleteLater();
        return;
    }
    if (pending.response.size() > kMaximumResponseBytes) {
        fail(pending.context, QStringLiteral("response_too_large"),
             QString::fromUtf8(u8"模型响应超过128 KiB"), status);
        reply->deleteLater();
        return;
    }
    if (reply->error() != QNetworkReply::NoError || status >= 400) {
        fail(pending.context, networkErrorCode(reply->error(), status),
             safeNetworkMessage(reply->error(), status), status);
        reply->deleteLater();
        return;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(pending.response, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        fail(pending.context, QStringLiteral("invalid_response_json"),
             QString::fromUtf8(u8"模型服务返回的JSON损坏"), status);
        reply->deleteLater();
        return;
    }
    if (pending.context.value("type").toString() == QStringLiteral("fetch_models")) {
        QJsonArray models;
        for (const auto& item : document.object().value("data").toArray()) {
            const QString id = item.toObject().value("id").toString();
            if (!id.isEmpty()) models.append(id);
        }
        QJsonObject response{{QStringLiteral("protocol_version"), kProtocolVersion},
                             {QStringLiteral("type"), QStringLiteral("fetch_models_result")},
                             {QStringLiteral("request_id"), requestId},
                             {QStringLiteral("ok"), true},
                             {QStringLiteral("models"), models}};
        send(response);
        reply->deleteLater();
        return;
    }
    qint64 inputTokens = -1;
    qint64 outputTokens = -1;
    bool reasoningOnly = false;
    const QString text = extractDecisionText(document.object(), pending.protocol,
                                             &inputTokens, &outputTokens, &reasoningOnly);
    QJsonParseError actionError;
    const auto actionDocument = QJsonDocument::fromJson(text.trimmed().toUtf8(), &actionError);
    const QJsonObject action = actionDocument.object();
    // Accept an integer, or a plain digit string, and ignore unrelated fields:
    // providers occasionally wrap the id or add commentary keys. Anything that
    // is not a whole number in range stays a hard failure.
    QJsonValue actionValue = action.value("action_id");
    if (actionValue.isUndefined()) {
        // Tolerate the close aliases some providers emit; the value still has to
        // be a whole number that exists in the locally generated catalog.
        for (const QString& alias : {QStringLiteral("actionId"), QStringLiteral("action"),
                                     QStringLiteral("id")}) {
            if (!action.value(alias).isUndefined()) {
                actionValue = action.value(alias);
                break;
            }
        }
    }
    double actionNumber = -1.0;
    bool actionNumberUsable = false;
    if (actionValue.isDouble()) {
        actionNumber = actionValue.toDouble();
        actionNumberUsable = true;
    } else if (actionValue.isString()) {
        bool converted = false;
        actionNumber = actionValue.toString().trimmed().toDouble(&converted);
        actionNumberUsable = converted;
    }
    if (actionError.error != QJsonParseError::NoError || !actionDocument.isObject() ||
        !actionNumberUsable || !std::isfinite(actionNumber) ||
        actionNumber < 0.0 ||
        actionNumber > static_cast<double>(std::numeric_limits<int>::max()) ||
        std::floor(actionNumber) != actionNumber) {
        // Report only a classification, never the model text itself.
        QString code = QStringLiteral("invalid_model_output");
        QString message = QString::fromUtf8(u8"模型没有返回规定的动作编号");
        if (text.trimmed().isEmpty()) {
            code = reasoningOnly ? QStringLiteral("reasoning_only_output")
                                 : QStringLiteral("empty_model_output");
            message = reasoningOnly
                ? QString::fromUtf8(u8"该模型只返回了思考过程，没有给出动作编号；"
                                    u8"请更换能直接返回动作编号的模型")
                : QString::fromUtf8(u8"模型返回了空内容");
        } else if (text.contains(QStringLiteral("```"))) {
            code = QStringLiteral("fenced_model_output");
            message = QString::fromUtf8(u8"模型把动作编号包在代码块里，未返回纯JSON");
        } else if (actionError.error == QJsonParseError::NoError &&
                   actionDocument.isObject() && !actionValue.isUndefined()) {
            code = QStringLiteral("malformed_action_value");
            message = QString::fromUtf8(u8"模型返回的动作编号不是合法整数");
        } else if (actionError.error != QJsonParseError::NoError) {
            code = QStringLiteral("invalid_model_json");
            message = QString::fromUtf8(u8"模型返回的内容不是JSON对象");
        } else if (actionError.error == QJsonParseError::NoError &&
                   actionDocument.isObject() && actionValue.isUndefined()) {
            code = QStringLiteral("missing_action_id");
            message = QString::fromUtf8(u8"模型返回的JSON缺少动作编号");
        }
        fail(pending.context, code, message, status);
        reply->deleteLater();
        return;
    }
    QJsonObject response = contextFields(pending.context);
    response[QStringLiteral("ok")] = true;
    response[QStringLiteral("action_id")] = static_cast<int>(actionNumber);
    response[QStringLiteral("latency_ms")] = latency;
    if (inputTokens >= 0) response[QStringLiteral("input_tokens")] = inputTokens;
    if (outputTokens >= 0) response[QStringLiteral("output_tokens")] = outputTokens;
    send(response);
    reply->deleteLater();
}

void AiService::fail(const QJsonObject& context, const QString& code,
                     const QString& message, int httpStatus) {
    QJsonObject response = contextFields(context);
    response[QStringLiteral("ok")] = false;
    response[QStringLiteral("error_code")] = code;
    response[QStringLiteral("message")] = message;
    if (context.contains(QStringLiteral("latency_ms"))) {
        response[QStringLiteral("latency_ms")] = context.value("latency_ms");
    }
    if (httpStatus > 0) response[QStringLiteral("http_status")] = httpStatus;
    send(response);
}

void AiService::send(QJsonObject message) {
    emit outputLine(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
}

QUrl AiService::requestUrl(const QJsonObject& credential, int protocol) {
    if (protocol == 0) {
#ifdef FPDZ_AI_SERVICE_TEST_HOOKS
        // Test-only override is never set by the product. It lets the automated
        // suite validate the official request shape without spending API quota.
        const QString testBase = qEnvironmentVariable("FPDZ_AI_TEST_OPENAI_BASE_URL");
        if (!testBase.isEmpty()) {
            QUrl testUrl(testBase);
            QString path = testUrl.path();
            while (path.endsWith('/')) path.chop(1);
            testUrl.setPath(path + QStringLiteral("/responses"));
            return testUrl;
        }
#endif
        return QUrl(QStringLiteral("https://api.openai.com/v1/responses"));
    }
    QUrl url(credential.value("request_url").toString().trimmed());
    QString path = url.path();
    const QString suffix = protocol == 2 ? QStringLiteral("/chat/completions")
                                         : QStringLiteral("/responses");
    if (!path.endsWith(suffix)) {
        while (path.endsWith('/')) path.chop(1);
        if (path.isEmpty()) path = QStringLiteral("/v1");
        path += suffix;
        url.setPath(path);
    }
    return url;
}

QUrl AiService::modelsUrl(const QJsonObject& credential) {
    const QString explicitUrl = credential.value("models_url").toString().trimmed();
    if (!explicitUrl.isEmpty()) return QUrl(explicitUrl);
    QUrl url = requestUrl(credential, credential.value("protocol").toInt());
    QString path = url.path();
    const QStringList endings{QStringLiteral("/chat/completions"), QStringLiteral("/responses")};
    for (const auto& ending : endings) {
        if (path.endsWith(ending)) path.chop(ending.size());
    }
    while (path.endsWith('/')) path.chop(1);
    url.setPath(path + QStringLiteral("/models"));
    return url;
}

QByteArray AiService::decisionBody(const QJsonObject& message,
                                   const QJsonObject& credential, int protocol) {
    const QString model = message.value("model").toString();
    const QString strength = message.value("strength").toString(QStringLiteral("medium"));
    const QJsonObject payloadObject = message.value("payload").toObject();
    const QString payload = QString::fromUtf8(
        QJsonDocument(payloadObject).toJson(QJsonDocument::Compact));
    const QString instructions = promptInstructions(
        payloadObject.value("player_count").toInt(),
        message.value("strategy_prompt").toString());
    QJsonObject body{{QStringLiteral("model"), model}};
    if (protocol == 0 || protocol == 1) {
        body[QStringLiteral("instructions")] = instructions;
        body[QStringLiteral("input")] = payload;
        body[QStringLiteral("store")] = false;
        // Reasoning-capable models spend output budget on hidden reasoning before
        // they emit the requested JSON. A small cap silently produced empty
        // answers, so keep a budget that still bounds cost while allowing the
        // decision to be produced in one request.
        body[QStringLiteral("max_output_tokens")] =
            (protocol == 0 || credential.value("standard_reasoning_parameter").toBool())
                ? 4096 : 2048;
        if (protocol == 0 || credential.value("standard_reasoning_parameter").toBool()) {
            body[QStringLiteral("reasoning")] = QJsonObject{{QStringLiteral("effort"), strength}};
        }
        if (protocol == 0 || credential.value("structured_output").toBool()) {
            body[QStringLiteral("text")] = QJsonObject{{QStringLiteral("format"), QJsonObject{
                {QStringLiteral("type"), QStringLiteral("json_schema")},
                {QStringLiteral("name"), QStringLiteral("doudizhu_action")},
                {QStringLiteral("strict"), true},
                {QStringLiteral("schema"), actionSchema()}}}};
        }
    } else {
        QJsonArray messages;
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                    {QStringLiteral("content"), instructions}});
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                    {QStringLiteral("content"), payload}});
        body[QStringLiteral("messages")] = messages;
        body[QStringLiteral("stream")] = false;
        // Thinking mode and JSON response formatting are mutually exclusive on
        // providers that follow the DeepSeek contract: requesting both made the
        // model return reasoning only, with no answer body. Choose by strength.
        const QString effort = compatibleReasoningEffort(strength);
        const bool thinkingRequested =
            credential.value("standard_reasoning_parameter").toBool() &&
            effort != QStringLiteral("none");
        body[QStringLiteral("max_tokens")] = thinkingRequested ? 8192 : 4096;
        if (credential.value("standard_reasoning_parameter").toBool()) {
            body[QStringLiteral("reasoning_effort")] = effort;
        }
        if (credential.value("structured_output").toBool() && !thinkingRequested) {
            // OpenAI-compatible Chat Completions endpoints commonly implement
            // JSON object mode; strict json_schema support is not guaranteed.
            body[QStringLiteral("response_format")] =
                QJsonObject{{QStringLiteral("type"), QStringLiteral("json_object")}};
        }
    }
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

QString AiService::extractDecisionText(const QJsonObject& response, int protocol,
                                       qint64* inputTokens, qint64* outputTokens,
                                       bool* reasoningOnly) {
    const QJsonObject usage = response.value("usage").toObject();
    if (protocol == 2) {
        if (inputTokens) *inputTokens = usage.value("prompt_tokens").toInteger(-1);
        if (outputTokens) *outputTokens = usage.value("completion_tokens").toInteger(-1);
        const QJsonArray choices = response.value("choices").toArray();
        if (choices.isEmpty()) return {};
        const QJsonObject message = choices.at(0).toObject()
            .value("message").toObject();
        const QString content = message.value("content").toString();
        if (content.trimmed().isEmpty() && reasoningOnly &&
            !message.value("reasoning_content").toString().trimmed().isEmpty()) {
            *reasoningOnly = true;
        }
        return content;
    }
    if (inputTokens) *inputTokens = usage.value("input_tokens").toInteger(-1);
    if (outputTokens) *outputTokens = usage.value("output_tokens").toInteger(-1);
    if (response.value("output_text").isString()) return response.value("output_text").toString();
    for (const auto& output : response.value("output").toArray()) {
        for (const auto& content : output.toObject().value("content").toArray()) {
            const auto object = content.toObject();
            if (object.value("type").toString() == QStringLiteral("output_text")) {
                return object.value("text").toString();
            }
        }
    }
    return {};
}

void AiService::applyAuthentication(QNetworkRequest& request,
                                    const QJsonObject& credential) {
    const int method = credential.value("auth_method").toInt();
    const QByteArray secret = credential.value("api_key").toString().toUtf8();
    if (method == 0) request.setRawHeader("Authorization", "Bearer " + secret);
    else if (method == 1) request.setRawHeader("api-key", secret);
    else if (method == 2) request.setRawHeader("x-api-key", secret);
    else if (method == 3) {
        const QString name = credential.value("custom_header_name").toString();
        if (CredentialStore::headerNameAllowed(name)) {
            request.setRawHeader(name.toLatin1(),
                credential.value("custom_header_value").toString().toUtf8());
        }
    }
}

} // namespace fpdz::ai_service
