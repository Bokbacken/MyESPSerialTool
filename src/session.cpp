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
        && port.open(p.readOnly ? QIODevice::ReadOnly : QIODevice::ReadWrite);
    if (ok && !p.readOnly) {
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
    case SessionState::Loaned: return "Loaned (reserved)";
    }
    return {};
}
void DisplayBuffer::append(const QByteArray &bytes) {
    QMutexLocker lock(&mutex);
    const qsizetype room = limit - buffer.size();
    buffer += bytes.left(room);
    dropped += bytes.size() - qMin(bytes.size(), room);
}
QByteArray DisplayBuffer::take(quint64 &droppedBytes) {
    QMutexLocker lock(&mutex);
    QByteArray result; result.swap(buffer);
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
        if (logger) logger->enqueue(bytes, received);
        if (display) display->append(bytes);
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
    if (logger) { logger->shutdown(); delete logger; logger = nullptr; }
    stopping = false; settings = checked; logDirectory = directory; attempts = 0;
    lastProblem.clear(); transition(SessionState::Disconnected);
}
void SessionWorker::monitor() {
    initialize();
    pollTimer->start();
    poll();
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
    initialize(); writeTimer->stop();
    stopping = true; transport->close(); stopping = false;
    transition(SessionState::ManuallyPaused);
}
void SessionWorker::startLog() {
    if (logger) { logger->shutdown(); delete logger; logger = nullptr; }
    if (!settings.logging) return;
    QString safe = settings.name;
    safe.replace(QRegularExpression("[^A-Za-z0-9_-]"), "_");
    const QString base = QDir(logDirectory).filePath(safe + "-"
        + QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz") + "-"
        + QUuid::createUuid().toString(QUuid::Id128));
    logger = new LogWorker(settings, base, this);
    connect(logger, &LogWorker::fault, this, &SessionWorker::problem);
    logger->start();
    emit problem("Raw capture: " + base + ".raw"
        + (settings.timestampMetadata ? " (reception UTC metadata in separate .meta)" : ""));
}
void SessionWorker::poll() {
    initialize();
    const auto available = transport->ports();
    QStringList names;
    for (const auto &p : available) names << p.port;
    emit portsAvailable(names);
    if (current != SessionState::Waiting) return;
    QList<PortIdentity> matches;
    for (const auto &p : available) {
        if (!settings.usbSerial.isEmpty()) {
            if (p.serial == settings.usbSerial && p.vid == settings.vid && p.pid == settings.pid)
                matches << p;
        } else if (p.port == settings.port || ("/dev/" + p.port) == settings.port) matches << p;
    }
    if (matches.size() != 1) {
        report(matches.isEmpty() ? "Selected device absent; no fallback port will be opened"
                                : "Ambiguous USB identity; refusing to open any port");
        return;
    }
    const auto chosen = matches.first();
    if (settings.usbSerial.isEmpty() && !chosen.serial.isEmpty() && chosen.vid >= 0 && chosen.pid >= 0) {
        settings.usbSerial = chosen.serial; settings.vid = chosen.vid; settings.pid = chosen.pid;
        emit identityBound(settings);
    }
    settings.port = chosen.port;
    transition(SessionState::Connecting); opening = true;
    QString error; ++attempts;
    const bool ok = transport->open(chosen.port, settings, error);
    opening = false;
    if (ok) {
        attempts = 0; lastProblem.clear(); startLog(); transition(SessionState::Connected);
        // Keep the timer at 1 Hz, but a connected session never enumerates ports.
    } else {
        report(QString("Open failed (%1/%2): %3").arg(attempts).arg(settings.retryLimit).arg(error));
        if (attempts >= settings.retryLimit) {
            transition(SessionState::Disconnected);
            emit problem("Retry limit reached. Press Connect / Resume to try again.");
        } else transition(SessionState::Waiting);
    }
}
void SessionWorker::lost(const QString &message) {
    report("Serial error: " + message);
    writeTimer->stop(); stopping = true; transport->close(); stopping = false;
    if (current != SessionState::ManuallyPaused && current != SessionState::Loaned) {
        attempts = 0; transition(SessionState::Waiting);
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
    if (logger) { logger->shutdown(); delete logger; logger = nullptr; }
}
