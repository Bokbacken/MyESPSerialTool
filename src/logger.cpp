#include "logger.h"
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

QString logFileStem(const Profile &profile, const QDateTime &started, const QString &id) {
    auto safe = [](QString value) {
        value.replace(QRegularExpression("[^A-Za-z0-9_-]"), "_");
        return value.left(40);
    };
    const QMap<QString, QString> fields = {
        {"profile", safe(profile.name)}, {"date", started.toUTC().toString("yyyyMMdd")},
        {"time", started.toUTC().toString("HHmmss-zzz")}, {"port", safe(profile.port)},
        {"serial", safe(profile.usbSerial)}, {"id", id}};
    QString result;
    const QRegularExpression token("\\{([a-z]+)\\}");
    auto matches = token.globalMatch(profile.logName);
    qsizetype position = 0;
    while (matches.hasNext()) {
        const auto match = matches.next();
        result += profile.logName.mid(position, match.capturedStart() - position);
        result += fields.value(match.captured(1));
        position = match.capturedEnd();
    }
    result += profile.logName.mid(position);
    if (!profile.logName.contains("{id}")) result += "-" + id;
    // Keep individual file names below Windows' component limit, including suffixes.
    if (result.size() > 200) result = result.left(160) + "-" + id;
    return result;
}

static QByteArray jsonRecord(const QJsonObject &object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}
static bool writeChecked(QFile &file, const QByteArray &bytes, QString &error) {
    if (file.write(bytes) != bytes.size() || !file.flush()) {
        error = file.fileName() + ": " + file.errorString(); return false;
    }
    return true;
}
static bool rotateFile(const QString &active, int retention, QString &error) {
    const QString oldest = active + "." + QString::number(retention);
    if (QFileInfo::exists(oldest) && !QFile::remove(oldest)) {
        error = "Cannot remove retained log: " + oldest; return false;
    }
    for (int i = retention - 1; i >= 0; --i) {
        const QString from = i == 0 ? active : active + "." + QString::number(i);
        if (QFileInfo::exists(from) && !QFile::rename(from, active + "." + QString::number(i + 1))) {
            error = "Cannot rotate log: " + from; return false;
        }
    }
    return true;
}

RotatingSink::RotatingSink(const Profile &p, const QString &b) : profile(p), base(b) {}
bool RotatingSink::begin(const QDateTime &started, QString &error) {
    if (session.isOpen()) return true;
    const auto directory = QFileInfo(base).absolutePath();
    if (!QDir().mkpath(directory)) { error = "Cannot create log directory: " + directory; return false; }
    session.setFileName(base + ".session");
    sessionStarted = started;
    if (!session.open(QIODevice::WriteOnly | QIODevice::Append)) { error = session.errorString(); return false; }
    QJsonObject settings;
    const auto values = profile.values();
    for (auto it = values.cbegin(); it != values.cend(); ++it) settings[it.key()] = it.value();
    return writeSession(jsonRecord({{"event", "start"}, {"hostUtc", started.toUTC().toString(Qt::ISODateWithMs)},
        {"profile", profile.name}, {"settings", settings}, {"rawBase", base + ".raw"}}), error);
}
bool RotatingSink::writeSession(const QByteArray &record, QString &error) {
    if (session.size() > 0 && session.size() + record.size() > qMax<qint64>(4096, profile.logBytes)) {
        session.close();
        if (!rotateFile(base + ".session", profile.logRetention, error)
            || !session.open(QIODevice::WriteOnly | QIODevice::Append)) {
            if (error.isEmpty()) error = session.errorString();
            return false;
        }
        QJsonObject settings;
        const auto values = profile.values();
        for (auto it = values.cbegin(); it != values.cend(); ++it) settings[it.key()] = it.value();
        const auto current = QJsonDocument::fromJson(record).object();
        if (!writeChecked(session, jsonRecord({{"event", "continuation"}, {"profile", profile.name},
            {"sessionStartedUtc", sessionStarted.toUTC().toString(Qt::ISODateWithMs)},
            {"hostUtc", current.value("hostUtc")}, {"settings", settings}}), error)) return false;
    }
    return writeChecked(session, record, error);
}
bool RotatingSink::event(const QString &message, const QDateTime &time, QString &error) {
    if (!begin(time, error)) return false;
    return writeSession(jsonRecord({{"event", message}, {"hostUtc", time.toUTC().toString(Qt::ISODateWithMs)},
                                    {"rawSegment", segment}, {"rawOffset", offset}}), error);
}
bool RotatingSink::finish(const QString &reason, const QDateTime &time, QString &error) {
    if (ended) return true;
    ended = true;
    if (!session.isOpen()) return false;
    QString textError, sessionError;
    const bool textOk = !profile.textLog || !text.isOpen()
        || writeText(textDecoder.feed({}, true), time, textError);
    const bool metadataOk = event("end: " + reason, time, sessionError);
    if (!metadataOk) error = sessionError;
    else if (!textOk) error = textError;
    return textOk && metadataOk;
}
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
    if (profile.textLog) {
        text.setFileName(base + ".text");
        if (!text.open(QIODevice::WriteOnly | QIODevice::Append)) {
            error = text.errorString(); return false;
        }
    }
    return true;
}
bool RotatingSink::rotate(QString &error) {
    raw.close(); metadata.close(); text.close();
    // Retention counts completed segments; the active segment is additional.
    for (const QString &extension : {QString(".raw"), QString(".meta"), QString(".text")})
        if (!rotateFile(base + extension, profile.logRetention, error)) return false;
    offset = 0; ++segment; return open(error);
}
bool RotatingSink::append(const QByteArray &bytes, const QDateTime &received, QString &error) {
    if (!begin(received, error) || !open(error)) return false;
    if (!segmentStarted.isValid()) segmentStarted = received;
    if (profile.logSeconds > 0 && offset > 0
        && segmentStarted.msecsTo(received) >= qint64(profile.logSeconds) * 1000) {
        if (!rotate(error)) return false;
        segmentStarted = received;
    }
    qsizetype position = 0;
    while (position < bytes.size()) {
        if (offset >= profile.logBytes) {
            if (!rotate(error)) return false;
            segmentStarted = received;
        }
        const qint64 length = qMin<qint64>(bytes.size() - position, profile.logBytes - offset);
        if (raw.write(bytes.constData() + position, length) != length || !raw.flush()) {
            error = "Raw log write failed: " + raw.errorString(); return false;
        }
        if (profile.timestampMetadata) {
            const QByteArray record = QString("%1 reception-utc offset=%2 length=%3 segment=%4\n")
                .arg(received.toUTC().toString(Qt::ISODateWithMs)).arg(offset).arg(length).arg(segment).toUtf8();
            if (metadata.write(record) != record.size() || !metadata.flush()) {
                error = "Timestamp metadata write failed: " + metadata.errorString(); return false;
            }
        }
        if (profile.textLog && !writeText(textDecoder.feed(bytes.mid(position, length)), received, error))
            return false;
        position += length; offset += length;
    }
    return true;
}
bool RotatingSink::writeText(const QString &decoded, const QDateTime &received, QString &error) {
    if (decoded.isEmpty()) return true;
    QString readable;
    for (QChar ch : decoded) {
        if ((ch.unicode() < 32 && ch != '\n' && ch != '\r' && ch != '\t') || ch.unicode() == 127)
            readable += QString("\\x%1").arg(ch.unicode(), 2, 16, QLatin1Char('0')).toUpper().replace("\\X", "\\x");
        else readable += ch;
    }
    return writeChecked(text, (received.toUTC().toString(Qt::ISODateWithMs)
        + " reception-utc " + readable + "\n").toUtf8(), error);
}
LogWorker::LogWorker(const Profile &p, const QString &b, QObject *parent)
    : QThread(parent), profile(p), base(b) {}
LogWorker::~LogWorker() { shutdown(); }
bool LogWorker::enqueue(const QByteArray &bytes, const QDateTime &received) {
    if (bytes.isEmpty()) return true;
    return enqueueRecord({bytes, received, false});
}
bool LogWorker::enqueueEvent(const QString &message, const QDateTime &time) {
    return enqueueRecord({message.left(4096).toUtf8(), time, true});
}
bool LogWorker::enqueueRecord(const Record &record) {
    QString message;
    {
        QMutexLocker lock(&mutex);
        if (stopping || failed) return false;
        if (record.bytes.size() > profile.logQueueBytes - queued || queue.size() >= 4096) {
            failed = true;
            endReason = "queue overflow"; ended = record.received;
            message = "LOGGING STOPPED: disk queue overflow; capture is incomplete. Manual pause, then Resume to start a new log.";
        } else {
            queue.enqueue(record); queued += record.bytes.size(); available.wakeOne(); return true;
        }
    }
    available.wakeOne(); emit fault(message); return false;
}
qint64 LogWorker::queuedBytes() const { QMutexLocker lock(&mutex); return queued; }
void LogWorker::shutdown(const QString &reason, const QDateTime &time) {
    { QMutexLocker lock(&mutex);
      if (!stopping && !failed) { endReason = reason; ended = time; }
      stopping = true; available.wakeOne(); }
    if (isRunning()) wait();
}
void LogWorker::run() {
    RotatingSink sink(profile, base);
    QString error;
    if (!sink.begin(started, error)) {
        { QMutexLocker lock(&mutex); failed = true; queue.clear(); queued = 0; }
        emit fault("LOGGING STOPPED: session metadata failed: " + error); return;
    }
    for (;;) {
        Record record;
        {
            QMutexLocker lock(&mutex);
            while (queue.isEmpty() && !stopping && !failed) available.wait(&mutex);
            if (queue.isEmpty() && (stopping || failed)) break;
            record = queue.dequeue(); queued -= record.bytes.size();
        }
        const bool ok = record.event ? sink.event(QString::fromUtf8(record.bytes), record.received, error)
                                     : sink.append(record.bytes, record.received, error);
        if (!ok) {
            { QMutexLocker lock(&mutex); failed = true; queue.clear(); queued = 0;
              endReason = "write failure: " + error; ended = record.received; }
            emit fault("LOGGING STOPPED: " + error + "; raw capture may be incomplete."); break;
        }
    }
    QString reason; QDateTime time;
    { QMutexLocker lock(&mutex); reason = endReason; time = ended; }
    if (!sink.finish(reason, time.isValid() ? time : started, error))
        emit fault("LOGGING STOPPED: session end metadata failed: " + error);
}
