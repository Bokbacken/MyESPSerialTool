#pragma once
#include "config.h"
#include "terminal.h"
#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QQueue>
#include <QThread>
#include <QWaitCondition>

QString logFileStem(const Profile &profile, const QDateTime &started, const QString &id);

class RotatingSink {
public:
    RotatingSink(const Profile &profile, const QString &base);
    bool append(const QByteArray &bytes, const QDateTime &received, QString &error);
    bool begin(const QDateTime &started, QString &error);
    bool event(const QString &message, const QDateTime &time, QString &error);
    bool finish(const QString &reason, const QDateTime &ended, QString &error);
    QString fileName() const { return base + ".raw"; }
private:
    Profile profile;
    QString base;
    QFile raw, metadata, text, session;
    Utf8Visible textDecoder;
    QDateTime sessionStarted, segmentStarted;
    qint64 offset = 0, segment = 0;
    bool ended = false;
    bool open(QString &error);
    bool rotate(QString &error);
    bool writeText(const QString &decoded, const QDateTime &received, QString &error);
    bool writeSession(const QByteArray &record, QString &error);
};

class LogWorker : public QThread {
    Q_OBJECT
public:
    LogWorker(const Profile &profile, const QString &base, QObject *parent = nullptr);
    ~LogWorker() override;
    bool enqueue(const QByteArray &bytes, const QDateTime &received);
    bool enqueueEvent(const QString &message, const QDateTime &time);
    void shutdown(const QString &reason = "closed",
                  const QDateTime &ended = QDateTime::currentDateTimeUtc());
    qint64 queuedBytes() const;
signals:
    void fault(const QString &message);
protected:
    void run() override;
private:
    struct Record { QByteArray bytes; QDateTime received; bool event = false; };
    Profile profile;
    QString base;
    mutable QMutex mutex;
    QWaitCondition available;
    QQueue<Record> queue;
    qint64 queued = 0;
    bool stopping = false, failed = false;
    QDateTime started = QDateTime::currentDateTimeUtc(), ended;
    QString endReason;
    bool enqueueRecord(const Record &record);
};
