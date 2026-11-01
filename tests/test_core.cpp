#include "config.h"
#include "logger.h"
#include "session.h"
#include "terminal.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>

class ArtifactDirectory {
public:
    QString path = QDir::current().absoluteFilePath("test-artifacts/" + QUuid::createUuid().toString(QUuid::Id128));
    ArtifactDirectory() { QDir().mkpath(path); }
    ~ArtifactDirectory() { QDir(path).removeRecursively(); }
    QString file(const QString &name) const { return QDir(path).filePath(name); }
};
static bool writeFile(const QString &name, const QByteArray &bytes) {
    QFile f(name); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
static QByteArray readFile(const QString &name) {
    QFile f(name); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll();
}

class FakeSerial : public SerialTransport {
    Q_OBJECT
public:
    QList<PortIdentity> devices;
    QStringList openedNames;
    QByteArray transmitted;
    Profile openedProfile;
    int openCalls = 0, closeCalls = 0, lineChanges = 0;
    mutable int enumerations = 0;
    bool opened = false, allowOpen = true;
    qint64 pending = 0;
    QList<PortIdentity> ports() const override { ++enumerations; return devices; }
    bool open(const QString &name, const Profile &p, QString &error) override {
        ++openCalls; openedNames << name; openedProfile = p;
        if (!allowOpen) { error = "fake busy port"; return false; }
        if (!p.readOnly) lineChanges += p.flowControl == "hardware" ? 1 : 2;
        opened = true; return true;
    }
    void close() override { opened = false; pending = 0; ++closeCalls; }
    bool isOpen() const override { return opened; }
    qint64 write(const QByteArray &bytes) override { transmitted += bytes; return bytes.size(); }
    qint64 pendingWrites() const override { return pending; }
    void inject(const QByteArray &bytes) { if (opened) emit received(bytes); }
    void unplug() { opened = false; emit transportError("fake unplug"); }
};

class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<Profile>(); qRegisterMetaType<SessionState>();
    }
    void defaultsAndStrictValidation() {
        Profile p; QString error;
        QVERIFY(Profile::fromValues("ESP", {}, p, error));
        QCOMPARE(p.baud, 115200); QCOMPARE(p.dataBits, 8);
        QCOMPARE(p.parity, QString("none")); QCOMPARE(p.flowControl, QString("none"));
        const QMap<QString, QString> invalid = {{"baud", "115200junk"}, {"dataBits", "9"},
            {"readOnly", "yes"}, {"parity", "banana"}, {"stopBits", "3"},
            {"flowControl", "xon"}, {"writeTimeoutMs", "0"}, {"retryLimit", "-1"},
            {"terminalRows", "10001"}, {"lineColumns", "999999"}, {"logBytes", "0"},
            {"logQueueBytes", "999999999"}, {"logRetention", "0"}, {"newline", "maybe"},
            {"vid", "65536"}, {"logDirectory", ""}, {"port", "COM1\nx=y"}};
        for (auto it = invalid.cbegin(); it != invalid.cend(); ++it) {
            Profile out; QVERIFY2(!Profile::fromValues("ESP", {{it.key(), it.value()}}, out, error),
                                 qPrintable(it.key()));
        }
        QVERIFY(!Profile::fromValues("bad]name", {}, p, error));
        QVERIFY(!Profile::fromValues("ESP", {{"usbSerial", "ABC"}}, p, error));
        QVERIFY(Profile::fromValues("ESP", {{"usbSerial", "ABC"}, {"vid", "10"}, {"pid", "20"}}, p, error));
    }
    void iniRoundtripBackupAndFailure() {
        ArtifactDirectory d;
        const QByteArray original = "; keep heading\r\n[app]\r\nformat=1 ; format note\r\ncustom=xyz\r\n"
            "\r\n[profile:ESP]\r\nbaud = 115200 # speed note\r\nextra=keep\r\n"
            "[future]\r\nunknown = literal\r\n";
        const auto path = d.file("settings.ini");
        QVERIFY(writeFile(path, original));
        IniFile ini; QString error;
        QVERIFY2(ini.load(path, error), qPrintable(error));
        QCOMPARE(ini.profiles.size(), 1);
        ini.profiles[0].baud = 9600;
        QVERIFY2(ini.save(error), qPrintable(error));
        QCOMPARE(readFile(path + ".bak"), original);
        const auto saved = readFile(path);
        QVERIFY(saved.contains("; keep heading"));
        QVERIFY(saved.contains("baud =9600 # speed note"));
        QVERIFY(saved.contains("custom=xyz")); QVERIFY(saved.contains("extra=keep"));
        QVERIFY(saved.contains("[future]\r\nunknown = literal"));
        IniFile loaded; QVERIFY2(loaded.load(path, error), qPrintable(error));
        QCOMPARE(loaded.profiles[0].baud, 9600);
        Profile second; second.name = "Device two"; loaded.profiles << second;
        QVERIFY(loaded.save(error));
        QCOMPARE(readFile(path + ".bak"), saved);
        QVERIFY(ini.load(path, error)); QCOMPARE(ini.profiles.size(), 2);
        const auto before = readFile(path);
        QVERIFY(QFile::remove(path + ".bak"));
        QVERIFY(QDir().mkdir(path + ".bak"));
        ini.profiles[0].baud = 4800;
        QVERIFY(!ini.save(error)); QCOMPARE(readFile(path), before);
        QVERIFY(!QFileInfo::exists(path + ".XXXXXX"));
        ini.profiles[0].baud = 0;
        QVERIFY(!ini.save(error)); QCOMPARE(readFile(path), before);
    }
    void invalidIniDoesNotMutateLoadedConfig() {
        ArtifactDirectory d;
        IniFile ini; QString error;
        QVERIFY(ini.load(d.file("absent.ini"), error)); QCOMPARE(ini.profiles[0].baud, 115200);
        const QList<QByteArray> invalid = {
            "[app]\nformat=2\n", "[profile:ESP]\nbaud=0\n", "[app]\nformat=1\nformat=1\n",
            "[profile:ESP]\nreadOnly=TRUE\n", "[app\nformat=1\n", "baud=115200\n",
            "[app]\nformat=1\n[app]\nformat=1\n", QByteArray("[app]\nx=") + char(0xff)};
        for (const auto &bytes : invalid) {
            QVERIFY(writeFile(d.file("invalid.ini"), bytes));
            QVERIFY(!ini.load(d.file("invalid.ini"), error)); QCOMPARE(ini.profiles[0].baud, 115200);
        }
        const auto bom = QByteArray::fromHex("efbbbf") + "[app]\r\nformat=1\r\n; BOM comment\r\n";
        QVERIFY(writeFile(d.file("bom.ini"), bom)); QVERIFY(ini.load(d.file("bom.ini"), error));
        QVERIFY(ini.save(error));
        QVERIFY(readFile(d.file("bom.ini")).startsWith(QByteArray::fromHex("efbbbf")));
        QVERIFY(readFile(d.file("bom.ini")).contains("; BOM comment\r\n"));
    }
    void incrementalUtf8AndBinaryVisibility() {
        Utf8Visible decoder;
        QCOMPARE(decoder.feed(QByteArray::fromHex("e282")), QString());
        QCOMPARE(decoder.feed(QByteArray::fromHex("ac")), QString::fromUtf8("€"));
        QCOMPARE(decoder.feed(QByteArray::fromHex("ff80c0")), QString("\\xFF\\x80\\xC0"));
        QCOMPARE(decoder.feed(QByteArray::fromHex("e2")), QString());
        QCOMPARE(decoder.feed("A"), QString("\\xE2A"));
        QCOMPARE(decoder.feed(QByteArray::fromHex("f09f")), QString());
        QCOMPARE(decoder.feed({}, true), QString("\\xF0\\x9F"));
        QCOMPARE(decoder.feed(QByteArray::fromHex("eda080")), QString("\\xED\\xA0\\x80"));
        Terminal terminal;
        terminal.feed(QByteArray("a\0b", 3));
        QCOMPARE(terminal.plainText(), QString("a\\x00b"));
        terminal.feed(QByteArray::fromHex("e282")); terminal.finish();
        QVERIFY(terminal.plainText().endsWith("\\xE2\\x82"));
        terminal.feed("<>&"); QVERIFY(terminal.html().contains("&lt;&gt;&amp;"));
        Terminal echoed;
        echoed.feed(QByteArray::fromHex("e282"));
        echoed.localEcho("TX");
        echoed.feed(QByteArray::fromHex("ac"));
        QCOMPARE(echoed.plainText(), QString::fromUtf8("TX€"));
        echoed.clear(); echoed.feed("\x1b[");
        echoed.localEcho("TX"); echoed.feed("31mred");
        QCOMPARE(echoed.plainText(), QString("TXred"));
        QVERIFY(echoed.html().contains("#cd5555"));
    }
    void ansiCursorEraseAndBounds() {
        Terminal terminal(3, 8);
        terminal.feed("abc\rZ"); QCOMPARE(terminal.plainText(), QString("Zbc"));
        terminal.feed("\x1b["); terminal.feed("31mR\x1b[0m");
        QVERIFY(terminal.html().contains("#cd5555"));
        terminal.feed("\x1b[2K\x1b[1Ghello\x1b[3G\x1b[K");
        QCOMPARE(terminal.plainText(), QString("he"));
        terminal.feed("\x1b[2;1Hsecond\x1b[1;1HX"); QCOMPARE(terminal.plainText(), QString("Xe\nsecond"));
        terminal.feed("\x1b[2J\x1b[H"); QCOMPARE(terminal.plainText(), QString("\n"));
        terminal.feed(QByteArray(50000, 'x'));
        QVERIFY(terminal.rowCount() <= 3); QVERIFY(terminal.longestLine() <= 8);
        terminal.feed("\x1b[99999;99999H!");
        QVERIFY(terminal.rowCount() <= 3); QVERIFY(terminal.longestLine() <= 8);
        terminal.feed("\x1b[" + QByteArray(10000, '0'));
        QVERIFY(terminal.rowCount() <= 3); QVERIFY(terminal.longestLine() <= 8);
        Terminal capped(10000, 16384);
        capped.feed("\x1b[9999;1H");
        QVERIFY(capped.rowCount() <= 10000);
        capped.feed(QByteArray(1200000, 'x'));
        QVERIFY(capped.cellCount() <= 1048576);
        Terminal normal(2000, 4096);
        normal.feed(QByteArray("short\n").repeated(1000));
        QCOMPARE(normal.rowCount(), 1001);
    }
    void stableIdentityPauseAndRetryLimit() {
        auto display = std::make_shared<DisplayBuffer>();
        auto *fake = new FakeSerial;
        fake->devices = {{"COM3", "", "ABC", 10, 20}};
        SessionWorker worker(display, fake);
        QSignalSpy problems(&worker, &SessionWorker::problem);
        Profile p; p.port = "COM3"; p.retryLimit = 2;
        worker.configure(p, "unused"); worker.resume(); worker.poll();
        QCOMPARE(worker.state(), SessionState::Connected);
        QCOMPARE(worker.profile().usbSerial, QString("ABC"));
        worker.pause(); fake->devices = {{"COM8", "", "ABC", 10, 20}};
        worker.poll(); worker.poll(); QCOMPARE(fake->openCalls, 1);
        QCOMPARE(worker.state(), SessionState::ManuallyPaused);
        worker.resume(); worker.poll(); QCOMPARE(fake->openedNames.last(), QString("COM8"));
        fake->unplug();
        fake->devices = {{"COM3", "", "OTHER", 10, 20}};
        worker.poll(); QCOMPARE(fake->openCalls, 2); QCOMPARE(worker.state(), SessionState::Waiting);
        fake->devices = {{"COM9", "", "ABC", 10, 20}, {"COM10", "", "ABC", 10, 20}};
        worker.poll(); QCOMPARE(fake->openCalls, 2);
        QVERIFY(problems.last().at(0).toString().contains("Ambiguous"));
        fake->devices = {{"COM9", "", "ABC", 10, 20}}; fake->allowOpen = false;
        worker.poll(); worker.poll(); worker.poll();
        QCOMPARE(fake->openCalls, 4); QCOMPARE(worker.state(), SessionState::Disconnected);
        worker.resume(); worker.poll(); QCOMPARE(fake->openCalls, 5);
    }
    void twoChannelsReadOnlyAndMailboxBound() {
        auto first = std::make_shared<DisplayBuffer>(), second = std::make_shared<DisplayBuffer>();
        auto *a = new FakeSerial; auto *b = new FakeSerial;
        a->devices = {{"A", "", "", -1, -1}}; b->devices = {{"B", "", "", -1, -1}};
        SessionWorker one(first, a), two(second, b);
        Profile p; p.port = "A"; p.readOnly = true; p.dtr = p.rts = true;
        one.configure(p, "unused"); p.port = "B"; p.readOnly = false; two.configure(p, "unused");
        one.resume(); two.resume(); one.poll(); two.poll();
        QCOMPARE(a->lineChanges, 0); QCOMPARE(b->lineChanges, 2);
        one.send("forbidden"); two.send(QByteArray::fromHex("0041ff"));
        QVERIFY(a->transmitted.isEmpty()); QCOMPARE(b->transmitted, QByteArray::fromHex("0041ff"));
        b->pending = 65535; two.send("xx");
        QCOMPARE(b->transmitted, QByteArray::fromHex("0041ff")); b->pending = 0;
        a->inject("one"); b->inject("two");
        quint64 dropped; QCOMPARE(first->take(dropped), QByteArray("one")); QCOMPARE(dropped, quint64(0));
        QCOMPARE(second->take(dropped), QByteArray("two"));
        one.pause(); b->inject("still receiving"); a->inject("not receiving");
        QCOMPARE(first->take(dropped), QByteArray()); QCOMPARE(second->take(dropped), QByteArray("still receiving"));
        second->append(QByteArray(400000, 'x'));
        QCOMPARE(second->take(dropped).size(), 262144); QCOMPARE(dropped, quint64(137856));
    }
    void monitorRateAndPauseSuppressesReconnect() {
        auto *fake = new FakeSerial; fake->devices = {{"A", "", "", -1, -1}};
        SessionWorker worker(std::make_shared<DisplayBuffer>(), fake);
        Profile p; p.port = "A"; worker.configure(p, "unused");
        worker.monitor();
        QCOMPARE(fake->enumerations, 1);
        for (int i = 0; i < 100; ++i) worker.resume();
        QCOMPARE(fake->enumerations, 1); QCOMPARE(fake->openCalls, 0);
        QTest::qWait(1100);
        QCOMPARE(fake->enumerations, 2); QCOMPARE(fake->openCalls, 1);
        worker.pause();
        QTest::qWait(1100);
        QCOMPARE(fake->enumerations, 3); QCOMPARE(fake->openCalls, 1);
        QCOMPARE(worker.state(), SessionState::ManuallyPaused);
    }
    void workerThreadAndWriteTimeout() {
        auto display = std::make_shared<DisplayBuffer>();
        auto *fake = new FakeSerial; fake->devices = {{"A", "", "", -1, -1}};
        auto *worker = new SessionWorker(display, fake);
        QThread thread; worker->moveToThread(&thread);
        connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
        QSignalSpy problems(worker, &SessionWorker::problem);
        thread.start();
        QMetaObject::invokeMethod(worker, [worker, fake] {
            Profile p; p.port = "A"; p.writeTimeoutMs = 100;
            worker->configure(p, "unused"); worker->resume(); worker->poll();
            fake->inject("thread bytes"); fake->pending = 10; worker->send("123");
        }, Qt::BlockingQueuedConnection);
        quint64 dropped; QCOMPARE(display->take(dropped), QByteArray("thread bytes"));
        QTRY_VERIFY_WITH_TIMEOUT(!problems.isEmpty(), 2000);
        QVERIFY(problems.last().at(0).toString().contains("timeout"));
        QMetaObject::invokeMethod(worker, &SessionWorker::shutdown, Qt::BlockingQueuedConnection);
        thread.quit(); QVERIFY(thread.wait(3000));
    }
    void exactRawRotationAndReceptionMetadata() {
        ArtifactDirectory d;
        Profile p; p.logBytes = 4; p.logRetention = 3; p.timestampMetadata = true;
        QString error;
        const auto base = d.file("capture");
        const auto time = QDateTime::fromString("2026-01-02T03:04:05.006Z", Qt::ISODateWithMs);
        RotatingSink sink(p, base);
        const auto bytes = QByteArray::fromHex("001b5b33316dff0d0ae282ac");
        QVERIFY2(sink.append(bytes.left(2), time, error), qPrintable(error));
        QVERIFY2(sink.append(bytes.mid(2), time, error), qPrintable(error));
        const QByteArray recovered = readFile(base + ".raw.2") + readFile(base + ".raw.1") + readFile(base + ".raw");
        QCOMPARE(recovered, bytes);
        QVERIFY(readFile(base + ".meta").contains("2026-01-02T03:04:05.006Z reception-utc"));
        QVERIFY(!readFile(base + ".raw").contains("offset="));
        QVERIFY(sink.append(QByteArray(20, 'z'), time, error));
        QVERIFY(QFileInfo::exists(base + ".raw.3")); QVERIFY(!QFileInfo::exists(base + ".raw.4"));
        for (int i = 1; i <= 3; ++i) QCOMPARE(readFile(base + ".raw." + QString::number(i)).size(), 4);
    }
    void logQueueOverflowAndIoFailure() {
        ArtifactDirectory d;
        Profile p; p.logQueueBytes = 3;
        LogWorker logger(p, d.file("overflow"));
        QSignalSpy overflow(&logger, &LogWorker::fault);
        const auto now = QDateTime::currentDateTimeUtc();
        QVERIFY(logger.enqueue("abc", now)); QVERIFY(!logger.enqueue("d", now));
        QCOMPARE(logger.queuedBytes(), 3); QCOMPARE(overflow.size(), 1);
        QVERIFY(overflow[0][0].toString().contains("overflow"));
        logger.start(); logger.shutdown();
        QCOMPARE(readFile(d.file("overflow.raw")), QByteArray("abc"));
        QVERIFY(writeFile(d.file("blocked"), "file, not directory"));
        LogWorker broken(p, d.file("blocked/capture"));
        QSignalSpy failure(&broken, &LogWorker::fault);
        broken.start(); QVERIFY(broken.enqueue("abc", now));
        QTRY_VERIFY_WITH_TIMEOUT(!failure.isEmpty(), 3000);
        QVERIFY(failure[0][0].toString().contains("LOGGING STOPPED"));
        QVERIFY(!broken.enqueue("x", now)); broken.shutdown();
        Profile metadataProfile; metadataProfile.timestampMetadata = true;
        const auto base = d.file("meta-failure");
        QVERIFY(QDir().mkdir(base + ".meta"));
        RotatingSink sink(metadataProfile, base); QString error;
        QVERIFY(!sink.append("raw", now, error));
        QVERIFY(!error.isEmpty());
        Profile rotationProfile; rotationProfile.logBytes = 3; rotationProfile.logRetention = 1;
        const auto rotateBase = d.file("rotation-failure");
        RotatingSink rotating(rotationProfile, rotateBase);
        QVERIFY(rotating.append("abc", now, error));
        QVERIFY(QDir().mkdir(rotateBase + ".raw.1"));
        QVERIFY(!rotating.append("d", now, error));
        QVERIFY(error.contains("Cannot remove retained log"));
        QCOMPARE(readFile(rotateBase + ".raw"), QByteArray("abc"));
    }
    void workerRawLogIndependentOfRenderedAnsi() {
        ArtifactDirectory d;
        auto display = std::make_shared<DisplayBuffer>();
        auto *fake = new FakeSerial; fake->devices = {{"A", "", "", -1, -1}};
        SessionWorker worker(display, fake);
        Profile p; p.port = "A"; p.logging = true; p.timestampMetadata = true;
        worker.configure(p, d.path); worker.resume(); worker.poll();
        const auto raw = QByteArray::fromHex("001b5b33316dff0d0ae282ac");
        fake->inject(raw); worker.shutdown();
        const auto logs = QDir(d.path).entryList({"*.raw"}, QDir::Files);
        QCOMPARE(logs.size(), 1); QCOMPARE(readFile(d.file(logs[0])), raw);
        quint64 dropped;
        Terminal rendered; rendered.feed(display->take(dropped)); rendered.clear();
        QCOMPARE(readFile(d.file(logs[0])), raw);
    }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "test_core.moc"
