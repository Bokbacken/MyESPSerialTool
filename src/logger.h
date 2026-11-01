#pragma once
#include "config.h"
#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QQueue>
#include <QThread>
#include <QWaitCondition>

class RotatingSink {
public:
    RotatingSink(const Profile &profile, const QString &base);
    bool append(const QByteArray &bytes, const QDateTime &received, QString &error);
    QString fileName() const { return base + ".raw"; }
private:
    Profile profile;
    QString base;
    QFile raw, metadata;
    qint64 offset = 0;
    bool open(QString &error);
    bool rotate(QString &error);
};

class LogWorker : public QThread {
    Q_OBJECT
public:
    LogWorker(const Profile &profile, const QString &base, QObject *parent = nullptr);
    ~LogWorker() override;
    bool enqueue(const QByteArray &bytes, const QDateTime &received);
    void shutdown();
    qint64 queuedBytes() const;
signals:
    void fault(const QString &message);
protected:
    void run() override;
private:
    struct Record { QByteArray bytes; QDateTime received; };
    Profile profile;
    QString base;
    mutable QMutex mutex;
    QWaitCondition available;
    QQueue<Record> queue;
    qint64 queued = 0;
    bool stopping = false, failed = false;
};
