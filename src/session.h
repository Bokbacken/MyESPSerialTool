#pragma once
#include "config.h"
#include "logger.h"
#include <QMutex>
#include <QObject>
#include <QSerialPort>
#include <QTimer>
#include <memory>

struct PortIdentity {
    QString port, description, serial;
    int vid = -1, pid = -1;
};

class SerialTransport : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual QList<PortIdentity> ports() const = 0;
    virtual bool open(const QString &port, const Profile &profile, QString &error) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual qint64 write(const QByteArray &bytes) = 0;
    virtual qint64 pendingWrites() const = 0;
signals:
    void received(const QByteArray &bytes);
    void writeProgress();
    void transportError(const QString &message);
};

class QtSerialTransport : public SerialTransport {
    Q_OBJECT
public:
    explicit QtSerialTransport(QObject *parent = nullptr);
    QList<PortIdentity> ports() const override;
    static QList<PortIdentity> enumerate();
    bool open(const QString &name, const Profile &profile, QString &error) override;
    void close() override;
    bool isOpen() const override { return port.isOpen(); }
    qint64 write(const QByteArray &bytes) override { return port.write(bytes); }
    qint64 pendingWrites() const override { return port.bytesToWrite(); }
private:
    QSerialPort port;
    bool suppressError = false;
    void drain();
};

enum class SessionState { Disconnected, Waiting, Connecting, Connected, ManuallyPaused, Loaned };
Q_DECLARE_METATYPE(SessionState)
QString stateName(SessionState state);

// One bounded mailbox per window avoids an unbounded queued GUI event per RX chunk.
class DisplayBuffer {
public:
    void append(const QByteArray &bytes);
    QByteArray take(quint64 &droppedBytes);
private:
    QMutex mutex;
    QByteArray buffer;
    quint64 dropped = 0;
    static constexpr qsizetype limit = 262144;
};

class SessionWorker : public QObject {
    Q_OBJECT
public:
    explicit SessionWorker(std::shared_ptr<DisplayBuffer> display,
                           SerialTransport *transport = nullptr, QObject *parent = nullptr);
    ~SessionWorker() override;
    SessionState state() const { return current; }
    Profile profile() const { return settings; }
public slots:
    void monitor();
    void configure(const Profile &profile, const QString &logDirectory);
    void resume();
    void pause();
    void setReadOnly(bool readOnly);
    void send(const QByteArray &bytes);
    void poll();
    void shutdown();
signals:
    void portsAvailable(const QStringList &ports);
    void stateChanged(SessionState state);
    void problem(const QString &message);
    void identityBound(const Profile &profile);
    void sent(const QByteArray &bytes);
private:
    SerialTransport *transport;
    std::shared_ptr<DisplayBuffer> display;
    QTimer *pollTimer = nullptr, *writeTimer = nullptr;
    Profile settings;
    QString logDirectory, lastProblem;
    SessionState current = SessionState::Disconnected;
    int attempts = 0;
    bool initialized = false, opening = false, stopping = false, reconnectAfterLoss = false;
    LogWorker *logger = nullptr;
    void initialize();
    void transition(SessionState state);
    void report(const QString &message);
    void lost(const QString &message);
    void startLog();
    void stopLog(const QString &reason);
};
