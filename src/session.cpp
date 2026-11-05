#include "session.h"
#include <QDir>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSerialPortInfo>
#include <QUuid>

QtSerialTransport::QtSerialTransport(QObject *parent) : SerialTransport(parent), port(this) {
    port.setReadBufferSize(65536);
    connect(&port, &QSerialPort::readyRead, this, &QtSerialTransport::drain);
    connect(&port, &QSerialPort::bytesWritten, this, &SerialTransport::writeProgress);
    connect(&port, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
        if (!suppressError && error != QSerialPort::NoError) emit transportError(port.errorString());
    });
}
void QtSerialTransport::drain() {
    int budget = 65536;
    while (port.bytesAvailable() > 0 && budget > 0) {
        const QByteArray bytes = port.read(qMin<qint64>(16384, port.bytesAvailable()));
        if (bytes.isEmpty()) break;
        budget -= bytes.size(); emit received(bytes);
    }
    if (port.bytesAvailable() > 0) QTimer::singleShot(0, this, &QtSerialTransport::drain);
}
QList<PortIdentity> QtSerialTransport::enumerate() {
    QList<PortIdentity> result;
    for (const auto &p : QSerialPortInfo::availablePorts())
        result << PortIdentity{p.portName(), p.description(), p.serialNumber(),
            p.hasVendorIdentifier() ? int(p.vendorIdentifier()) : -1,
            p.hasProductIdentifier() ? int(p.productIdentifier()) : -1};
    return result;
}
QList<PortIdentity> QtSerialTransport::ports() const { return enumerate(); }
bool QtSerialTransport::open(const QString &name, const Profile &p, QString &error) {
    suppressError = true;
    port.setPortName(name);
    const QMap<QString, QSerialPort::Parity> parity = {{"none", QSerialPort::NoParity},
        {"even", QSerialPort::EvenParity}, {"odd", QSerialPort::OddParity},
        {"space", QSerialPort::SpaceParity}, {"mark", QSerialPort::MarkParity}};
    const QMap<QString, QSerialPort::StopBits> stops = {{"1", QSerialPort::OneStop},
        {"1.5", QSerialPort::OneAndHalfStop}, {"2", QSerialPort::TwoStop}};
    const QMap<QString, QSerialPort::FlowControl> flow = {{"none", QSerialPort::NoFlowControl},
        {"hardware", QSerialPort::HardwareControl}, {"software", QSerialPort::SoftwareControl}};
    bool ok = port.setBaudRate(p.baud) && port.setDataBits(QSerialPort::DataBits(p.dataBits))
        && port.setParity(parity.value(p.parity)) && port.setStopBits(stops.value(p.stopBits))
        && port.setFlowControl(flow.value(p.flowControl))
        && port.open(QIODevice::ReadWrite);
    if (ok) {
        ok = port.setDataTerminalReady(p.dtr);
        if (ok && p.flowControl != "hardware") ok = port.setRequestToSend(p.rts);
    }
    if (!ok) { error = port.errorString(); port.close(); }
    suppressError = false; return ok;
}
void QtSerialTransport::close() {
    suppressError = true;
    drain(); port.close();
    suppressError = false;
}
QString stateName(SessionState state) {
    switch (state) {
    case SessionState::Disconnected: return "Disconnected";
    case SessionState::Waiting: return "Waiting for selected device";
    case SessionState::Connecting: return "Connecting";
    case SessionState::Connected: return "Connected";
    case SessionState::ManuallyPaused: return "Manually paused";
    case SessionState::Loaned: return "Port lent to esptool";
    }
    return {};
}
void DisplayBuffer::append(const QByteArray &bytes, const QDateTime &received) {
    QMutexLocker lock(&mutex);
    const qsizetype accepted = records.size() < 4096 ? qMin(bytes.size(), limit - bufferedBytes) : 0;
    if (accepted > 0) records << Record{bytes.left(accepted), received};
    bufferedBytes += accepted;
    dropped += bytes.size() - accepted;
}
void DisplayBuffer::setLimit(qsizetype bytes) {
    QMutexLocker lock(&mutex);
    limit = bytes;
    while (bufferedBytes > limit && !records.isEmpty()) {
        auto &last = records.last();
        const qsizetype removed = qMin(last.bytes.size(), bufferedBytes - limit);
        last.bytes.chop(removed);
        bufferedBytes -= removed;
        dropped += removed;
        if (last.bytes.isEmpty()) records.removeLast();
    }
}
QByteArray DisplayBuffer::take(quint64 &droppedBytes) {
    QByteArray result;
    for (const auto &record : takeRecords(droppedBytes)) result += record.bytes;
    return result;
}
QVector<DisplayBuffer::Record> DisplayBuffer::takeRecords(quint64 &droppedBytes) {
    QMutexLocker lock(&mutex);
    QVector<Record> result; result.swap(records);
    bufferedBytes = 0;
    droppedBytes = dropped; dropped = 0; return result;
}
SessionWorker::SessionWorker(std::shared_ptr<DisplayBuffer> d, SerialTransport *t, QObject *parent)
    : QObject(parent), transport(t), display(std::move(d)) {
    if (transport) transport->setParent(this);
}
SessionWorker::~SessionWorker() { shutdown(); }
void SessionWorker::initialize() {
    if (initialized) return;
    initialized = true;
    if (!transport) transport = new QtSerialTransport(this);
    pollTimer = new QTimer(this); pollTimer->setInterval(1000);
    pollTimer->setTimerType(Qt::PreciseTimer);
    writeTimer = new QTimer(this); writeTimer->setSingleShot(true);
    connect(pollTimer, &QTimer::timeout, this, &SessionWorker::poll);
    connect(writeTimer, &QTimer::timeout, this, [this] {
        if (transport->pendingWrites() > 0) lost("Write timeout; transmission may be incomplete");
    });
    connect(transport, &SerialTransport::writeProgress, this, [this] {
        if (transport->pendingWrites() == 0) writeTimer->stop();
    });
    connect(transport, &SerialTransport::transportError, this, [this](const QString &error) {
        if (!opening && !stopping) lost(error);
    });
    connect(transport, &SerialTransport::received, this, [this](const QByteArray &bytes) {
        if (bytes.isEmpty()) return;
        const auto received = QDateTime::currentDateTimeUtc();
        if (logger && settings.logging) logger->enqueue(bytes, received);
        if (display) display->append(bytes, received);
    });
}
void SessionWorker::transition(SessionState state) {
    if (current == state) return;
    current = state; emit stateChanged(state);
}
void SessionWorker::report(const QString &message) {
    if (lastProblem != message) { lastProblem = message; emit problem(message); }
}
void SessionWorker::configure(const Profile &p, const QString &directory) {
    initialize();
    Profile checked; QString error;
    if (!Profile::fromValues(p.name, p.values(), checked, error)) { report(error); return; }
    stopping = true; transport->close(); writeTimer->stop();
    stopLog("settings reconfigured");
    stopping = false; settings = checked; logDirectory = directory; attempts = 0;
    if (display) display->setLimit(settings.displayQueueBytes);
    reconnectAfterLoss = false;
    toolRestorePending = false;
    lastProblem.clear(); transition(SessionState::Disconnected);
}
void SessionWorker::monitor() {
    initialize();
    pollTimer->start();
    poll();
}
bool SessionWorker::prepareTool(QString &error) {
    initialize();
    if (current == SessionState::Loaned) {
        error = "The serial port is already lent to another esptool operation.";
        return false;
    }
    restoreAfterTool = current == SessionState::Connected
        || current == SessionState::Waiting || current == SessionState::Connecting;
    manualPauseDuringTool = current == SessionState::ManuallyPaused;
    attempts = 0;
    if (pollTimer) pollTimer->stop();
    if (writeTimer) writeTimer->stop();
    stopping = true;
    transport->close();
    stopping = false;
    stopLog("serial port handed to esptool");
    transition(SessionState::Loaned);
    return true;
}
void SessionWorker::finishTool() {
    if (current != SessionState::Loaned) return;
    const bool reconnect = restoreAfterTool && !manualPauseDuringTool;
    restoreAfterTool = false;
    if (reconnect) {
        reconnectAfterLoss = true;
        toolRestorePending = true;
        transition(SessionState::Waiting);
        if (pollTimer) pollTimer->start();
        poll();
    } else {
        toolRestorePending = false;
        transition(manualPauseDuringTool ? SessionState::ManuallyPaused
                                         : SessionState::Disconnected);
    }
    manualPauseDuringTool = false;
}
void SessionWorker::resume() {
    initialize();
    if (current == SessionState::Connected || current == SessionState::Connecting
        || current == SessionState::Loaned) return;
    attempts = 0; lastProblem.clear(); transition(SessionState::Waiting);
    // Even repeated Resume clicks cannot cause port enumeration/open attempts above 1 Hz.
    if (!pollTimer->isActive()) pollTimer->start();
}
void SessionWorker::pause() {
    initialize();
    if (current == SessionState::Loaned) {
        restoreAfterTool = false;
        manualPauseDuringTool = true;
        toolRestorePending = false;
        emit toolCancelRequested();
        return;
    }
    writeTimer->stop();
    stopping = true; transport->close(); stopping = false;
    reconnectAfterLoss = false;
    toolRestorePending = false;
    stopLog("manually paused");
    transition(SessionState::ManuallyPaused);
}
void SessionWorker::setReadOnly(bool readOnly) {
    settings.readOnly = readOnly;
    if (logger) logger->enqueueEvent(readOnly ? "read-only send gate enabled" : "read-only send gate disabled",
                                      QDateTime::currentDateTimeUtc());
}
void SessionWorker::setAutoReconnect(bool enabled) {
    settings.autoReconnect = enabled;
    if (logger) logger->enqueueEvent(enabled ? "automatic reconnect enabled" : "automatic reconnect disabled",
                                     QDateTime::currentDateTimeUtc());
    if (!enabled && reconnectAfterLoss && !toolRestorePending && current == SessionState::Waiting) {
        stopLog("automatic reconnect disabled while waiting");
        transition(SessionState::Disconnected);
    }
}
void SessionWorker::stopLog(const QString &reason) {
    if (!logger) return;
    logger->shutdown(reason, QDateTime::currentDateTimeUtc()); delete logger; logger = nullptr;
}
void SessionWorker::splitLog() {
    initialize();
    if (current != SessionState::Connected || !logger) {
        report("Split log ignored: the session is not connected and recording.");
        return;
    }
    const QString previous = logBase;
    const auto now = QDateTime::currentDateTimeUtc();
    logger->enqueueEvent("log split requested by user; continuing in a new log set", now);
    stopLog("split by user");
    startLog();
    logger->enqueueEvent("continued after user split from " + QFileInfo(previous).fileName(), now);
    logger->enqueueEvent("connected " + settings.port + " (connection kept open across split)", now);
    // Same worker thread as the receive path, so the marker sits exactly between the two log sets.
    if (display) display->append("\n----- log split " + now.toString(Qt::ISODateWithMs).toUtf8()
        + " UTC: new log set " + QFileInfo(logBase).fileName().toUtf8() + " -----\n", now);
    emit logSplit(previous);
}
void SessionWorker::startLog() {
    if (logger) return;
    const QString requested = QDir(logDirectory).filePath(logFileStem(settings,
        QDateTime::currentDateTimeUtc(), QUuid::createUuid().toString(QUuid::Id128)));
    QString base = requested;
    int suffix = 0;
    while (QFileInfo::exists(base + ".session") || QFileInfo::exists(base + settings.logExtension)
        || QFileInfo::exists(base + ".meta") || QFileInfo::exists(base + settings.rawLogExtension)
        || QFileInfo::exists(base + ".lock")) {
        base = requested + "-" + QString::number(++suffix);
    }
    logBase = base;
    logger = new LogWorker(settings, base, this);
    connect(logger, &LogWorker::fault, this, &SessionWorker::problem);
    logger->start();
    emit logFileChanged(settings.logging
        ? base + (settings.rawLogging() ? settings.rawLogExtension : settings.logExtension) : QString{});
    emit problem("Session metadata: " + base + ".session");
    if (settings.logging) {
        if (settings.rawLogging()) emit problem("Raw exact byte capture: " + base + settings.rawLogExtension
            + (settings.timestampMetadata ? " (reception UTC offsets in .meta)" : ""));
        if (settings.textLogging()) emit problem("Readable UTF-8 text capture: " + base + settings.logExtension
            + (settings.textTimestamp ? " (host reception UTC timestamps)" : " (timestamps off)"));
    }
}
void SessionWorker::poll() {
    initialize();
    const auto available = transport->ports();
    QStringList names;
    for (const auto &p : available) names << p.port;
    emit portsAvailable(names);
    QList<PortIdentity> matches;
    for (const auto &p : available) {
        if (!settings.usbSerial.isEmpty()) {
            if (p.serial == settings.usbSerial && p.vid == settings.vid && p.pid == settings.pid)
                matches << p;
        } else if (p.port == settings.port || ("/dev/" + p.port) == settings.port) matches << p;
    }
    if (current == SessionState::Connected) {
        if (matches.size() == 1 && matches.first().port == settings.port) return;
        lost(matches.isEmpty() ? "Selected device disappeared"
             : matches.size() > 1 ? "Selected USB identity became ambiguous"
                                  : "Selected device moved to another port");
    }
    if (current != SessionState::Waiting) return;
    const bool shouldReconnect = toolRestorePending
        || (reconnectAfterLoss ? settings.autoReconnect : settings.autoConnect);
    if (matches.size() != 1) {
        report(matches.isEmpty() ? "Selected device absent; no fallback port will be opened"
                                : "Ambiguous USB identity; refusing to open any port");
        if (!shouldReconnect) transition(SessionState::Disconnected);
        return;
    }
    const auto chosen = matches.first();
    if (settings.usbSerial.isEmpty() && !chosen.serial.isEmpty() && chosen.vid >= 0 && chosen.pid >= 0) {
        int duplicates = 0;
        for (const auto &p : available)
            if (p.serial == chosen.serial && p.vid == chosen.vid && p.pid == chosen.pid) ++duplicates;
        if (duplicates != 1) {
            report("Ambiguous newly discovered USB identity; refusing to bind or open any port");
            if (!shouldReconnect) transition(SessionState::Disconnected);
            return;
        }
        settings.usbSerial = chosen.serial; settings.vid = chosen.vid; settings.pid = chosen.pid;
        emit identityBound(settings);
    }
    settings.port = chosen.port;
    emit selectedPortChanged(chosen.port);
    transition(SessionState::Connecting); opening = true;
    // The bounded disk queue is ready before open can deliver any initial RX bytes.
    startLog();
    logger->enqueueEvent("opening " + chosen.port, QDateTime::currentDateTimeUtc());
    QString error; ++attempts;
    const bool ok = transport->open(chosen.port, settings, error);
    opening = false;
    if (ok) {
        attempts = 0; lastProblem.clear();
        reconnectAfterLoss = true;
        toolRestorePending = false;
        logger->enqueueEvent("connected " + chosen.port, QDateTime::currentDateTimeUtc());
        transition(SessionState::Connected);
    } else {
        report(QString("Open failed (%1/%2): %3").arg(attempts).arg(settings.retryLimit).arg(error));
        logger->enqueueEvent("open failed: " + error, QDateTime::currentDateTimeUtc());
        if (attempts >= settings.retryLimit || !shouldReconnect) {
            toolRestorePending = false;
            stopLog(!shouldReconnect ? "open failed; automatic reconnect disabled" : "retry limit reached");
            transition(SessionState::Disconnected);
            emit problem("Connection stopped. Press Connect / Resume to try again.");
        } else transition(SessionState::Waiting);
    }
}
void SessionWorker::lost(const QString &message) {
    report("Serial error: " + message);
    if (logger) logger->enqueueEvent("interruption: " + message, QDateTime::currentDateTimeUtc());
    writeTimer->stop(); stopping = true; transport->close(); stopping = false;
    if (current != SessionState::ManuallyPaused && current != SessionState::Loaned) {
        const bool shouldReconnect = toolRestorePending
            || (reconnectAfterLoss ? settings.autoReconnect : settings.autoConnect);
        attempts = 0; transition(shouldReconnect ? SessionState::Waiting : SessionState::Disconnected);
        if (!shouldReconnect) stopLog("interrupted; automatic reconnect disabled");
        else if (settings.newLogOnReconnect) stopLog("interrupted; new log on reconnect");
        if (!pollTimer->isActive()) pollTimer->start();
    }
}
void SessionWorker::send(const QByteArray &bytes) {
    initialize();
    if (settings.readOnly) { report("Read-only session: send blocked; DTR/RTS are not modified"); return; }
    if (current != SessionState::Connected) { report("Send blocked: session is not connected"); return; }
    if (bytes.size() > 65536 - transport->pendingWrites()) {
        report("Send blocked: bounded transmit queue is full"); return;
    }
    const auto written = transport->write(bytes);
    if (written != bytes.size()) { lost("Write failed or accepted only a partial message"); return; }
    emit sent(bytes);
    if (transport->pendingWrites() > 0 && !writeTimer->isActive()) writeTimer->start(settings.writeTimeoutMs);
}
void SessionWorker::shutdown() {
    stopping = true;
    if (pollTimer) pollTimer->stop();
    if (writeTimer) writeTimer->stop();
    if (transport) transport->close();
    stopLog("window/session closed");
}
