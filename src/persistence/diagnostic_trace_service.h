#pragma once

#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVector>

namespace fpdz {

class DiagnosticTraceService : public QObject {
    Q_OBJECT
public:
    explicit DiagnosticTraceService(QObject* parent = nullptr);
    ~DiagnosticTraceService() override;

    void init(const QString& logDir);
    void record(QJsonObject event);
    void flush();
    QString traceFilePath() const;
    QString sessionId() const;

private:
    void rotateIfNeeded(qint64 incomingBytes);

    QString m_logDir;
    QString m_sessionId;
    QVector<QByteArray> m_pendingLines;
    qint64 m_pendingBytes = 0;
    quint64 m_droppedEvents = 0;
    QTimer m_flushTimer;
};

} // namespace fpdz
