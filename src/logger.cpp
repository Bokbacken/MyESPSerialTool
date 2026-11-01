#include "logger.h"
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>

RotatingSink::RotatingSink(const Profile &p, const QString &b) : profile(p), base(b) {}
bool RotatingSink::open(QString &error) {
    if (raw.isOpen()) return true;
    const QString directory = QFileInfo(base).absolutePath();
    if (!QDir().mkpath(directory)) { error = "Cannot create log directory: " + directory; return false; }
    raw.setFileName(base + ".raw");
    if (!raw.open(QIODevice::WriteOnly | QIODevice::Append)) { error = raw.errorString(); return false; }
    offset = raw.size();
    if (profile.timestampMetadata) {
        metadata.setFileName(base + ".meta");
        if (!metadata.open(QIODevice::WriteOnly | QIODevice::Append)) {
            error = metadata.errorString(); raw.close(); return false;
        }
    }
    return true;
}
bool RotatingSink::rotate(QString &error) {
    raw.close(); metadata.close();
    // Retention counts completed segments; the active segment is additional.
    for (const QString &extension : {QString(".raw"), QString(".meta")}) {
        const QString active = base + extension;
        const QString oldest = active + "." + QString::number(profile.logRetention);
        if (QFileInfo::exists(oldest) && !QFile::remove(oldest)) {
            error = "Cannot remove retained log: " + oldest; return false;
        }
        for (int i = profile.logRetention - 1; i >= 0; --i) {
            const QString from = i == 0 ? active : active + "." + QString::number(i);
            const QString to = active + "." + QString::number(i + 1);
            if (QFileInfo::exists(from) && !QFile::rename(from, to)) {
                error = "Cannot rotate log: " + from; return false;
            }
        }
    }
    offset = 0; return open(error);
}
bool RotatingSink::append(const QByteArray &bytes, const QDateTime &received, QString &error) {
    if (!open(error)) return false;
    qsizetype position = 0;
    while (position < bytes.size()) {
        if (offset >= profile.logBytes && !rotate(error)) return false;
        const qint64 length = qMin<qint64>(bytes.size() - position, profile.logBytes - offset);
        if (raw.write(bytes.constData() + position, length) != length || !raw.flush()) {
            error = "Raw log write failed: " + raw.errorString(); return false;
        }
        if (profile.timestampMetadata) {
            const QByteArray record = QString("%1 reception-utc offset=%2 length=%3\n")
                .arg(received.toUTC().toString(Qt::ISODateWithMs)).arg(offset).arg(length).toUtf8();
            if (metadata.write(record) != record.size() || !metadata.flush()) {
                error = "Timestamp metadata write failed: " + metadata.errorString(); return false;
            }
        }
        position += length; offset += length;
    }
    return true;
}
LogWorker::LogWorker(const Profile &p, const QString &b, QObject *parent)
    : QThread(parent), profile(p), base(b) {}
LogWorker::~LogWorker() { shutdown(); }
bool LogWorker::enqueue(const QByteArray &bytes, const QDateTime &received) {
    if (bytes.isEmpty()) return true;
    QString message;
    {
        QMutexLocker lock(&mutex);
        if (stopping || failed) return false;
        if (bytes.size() > profile.logQueueBytes - queued || queue.size() >= 4096) {
            failed = true;
            message = "LOGGING STOPPED: disk queue overflow; raw capture is incomplete. Reconnect to start a new log.";
        } else {
            queue.enqueue({bytes, received}); queued += bytes.size(); available.wakeOne(); return true;
        }
    }
    available.wakeOne(); emit fault(message); return false;
}
qint64 LogWorker::queuedBytes() const { QMutexLocker lock(&mutex); return queued; }
void LogWorker::shutdown() {
    { QMutexLocker lock(&mutex); stopping = true; available.wakeOne(); }
    if (isRunning()) wait();
}
void LogWorker::run() {
    RotatingSink sink(profile, base);
    for (;;) {
        Record record;
        {
            QMutexLocker lock(&mutex);
            while (queue.isEmpty() && !stopping && !failed) available.wait(&mutex);
            if (queue.isEmpty() && (stopping || failed)) break;
            record = queue.dequeue(); queued -= record.bytes.size();
        }
        QString error;
        if (!sink.append(record.bytes, record.received, error)) {
            { QMutexLocker lock(&mutex); failed = true; queue.clear(); queued = 0; }
            emit fault("LOGGING STOPPED: " + error + "; raw capture may be incomplete."); break;
        }
    }
}
