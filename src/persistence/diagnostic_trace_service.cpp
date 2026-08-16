#include <persistence/diagnostic_trace_service.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QUuid>

namespace fpdz {

namespace {
constexpr qint64 kMaximumTraceBytes = 10 * 1024 * 1024;
constexpr int kTraceBackupCount = 5;
constexpr qint64 kMaximumPendingBytes = 2 * 1024 * 1024;
constexpr int kMaximumPendingLines = 4096;
}

DiagnosticTraceService::DiagnosticTraceService(QObject* parent) : QObject(parent) {
    m_flushTimer.setInterval(250);
    m_flushTimer.setSingleShot(true);
    connect(&m_flushTimer, &QTimer::timeout, this, &DiagnosticTraceService::flush);
}

DiagnosticTraceService::~DiagnosticTraceService() {
    flush();
}

void DiagnosticTraceService::init(const QString& logDir) {
    m_logDir = logDir;
    QDir().mkpath(m_logDir);
    m_sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QJsonObject event;
    event[QStringLiteral("type")] = QStringLiteral("session_start");
    event[QStringLiteral("executable")] = QCoreApplication::applicationFilePath();
    event[QStringLiteral("application_version")] = QCoreApplication::applicationVersion();
    record(event);
}

void DiagnosticTraceService::record(QJsonObject event) {
    if (m_logDir.isEmpty()) return;
    event[QStringLiteral("timestamp")] =
        QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    event[QStringLiteral("session_id")] = m_sessionId;
    const QByteArray line = QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
    if (m_pendingLines.size() >= kMaximumPendingLines ||
        m_pendingBytes + line.size() > kMaximumPendingBytes) {
        ++m_droppedEvents;
        return;
    }
    m_pendingLines.push_back(line);
    m_pendingBytes += line.size();
    if (!m_flushTimer.isActive()) m_flushTimer.start();
}

void DiagnosticTraceService::flush() {
    if ((m_pendingLines.isEmpty() && m_droppedEvents == 0) || m_logDir.isEmpty()) return;

    if (m_droppedEvents > 0) {
        QJsonObject dropped{
            {QStringLiteral("type"), QStringLiteral("trace_overflow")},
            {QStringLiteral("dropped_count"), static_cast<qint64>(m_droppedEvents)},
            {QStringLiteral("timestamp"),
             QDateTime::currentDateTime().toString(Qt::ISODateWithMs)},
            {QStringLiteral("session_id"), m_sessionId}
        };
        const QByteArray line = QJsonDocument(dropped).toJson(QJsonDocument::Compact) + '\n';
        m_pendingLines.push_back(line);
        m_pendingBytes += line.size();
        m_droppedEvents = 0;
    }

    qint64 incomingBytes = 0;
    for (const auto& line : m_pendingLines) incomingBytes += line.size();
    rotateIfNeeded(incomingBytes);

    QFile file(traceFilePath());
    if (!file.open(QIODevice::Append)) return;
    for (const auto& line : m_pendingLines) file.write(line);
    file.flush();
    m_pendingLines.clear();
    m_pendingBytes = 0;
}

QString DiagnosticTraceService::traceFilePath() const {
    return m_logDir + QStringLiteral("/input_trace.jsonl");
}

QString DiagnosticTraceService::sessionId() const {
    return m_sessionId;
}

void DiagnosticTraceService::rotateIfNeeded(qint64 incomingBytes) {
    QFile current(traceFilePath());
    if (!current.exists() || current.size() + incomingBytes <= kMaximumTraceBytes) return;

    QFile::remove(traceFilePath() + QStringLiteral(".%1").arg(kTraceBackupCount));
    for (int index = kTraceBackupCount - 1; index >= 1; --index) {
        QFile::rename(traceFilePath() + QStringLiteral(".%1").arg(index),
                      traceFilePath() + QStringLiteral(".%1").arg(index + 1));
    }
    QFile::rename(traceFilePath(), traceFilePath() + QStringLiteral(".1"));
}

} // namespace fpdz
