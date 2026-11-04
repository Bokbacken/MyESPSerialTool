#include "window.h"
#include "esptool_dialog.h"
#include "flashplan_dialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTextEdit>
#include <QTextCursor>
#include <QFileDialog>
#include <QTableWidget>
#include <QInputDialog>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QtEndian>
#include <algorithm>

class WindowSerial : public SerialTransport {
public:
    QList<PortIdentity> ports() const override { return {{"TEST", "", "", -1, -1}}; }
    bool open(const QString &, const Profile &, QString &) override { opened = true; return true; }
    void close() override { opened = false; }
    bool isOpen() const override { return opened; }
    qint64 write(const QByteArray &bytes) override { return bytes.size(); }
    qint64 pendingWrites() const override { return 0; }
    bool opened = false;
};

static QByteArray sampleEsp32S3App(quint16 chipId = 9) {
    QByteArray bytes(24, '\0');
    bytes[0] = char(0xe9);
    bytes[1] = 1;
    bytes[23] = 1;
    qToLittleEndian<quint16>(chipId, reinterpret_cast<uchar *>(bytes.data() + 12));
    QByteArray segmentHeader(8, '\0');
    qToLittleEndian<quint32>(4, reinterpret_cast<uchar *>(segmentHeader.data() + 4));
    bytes += segmentHeader + QByteArray::fromHex("01020304");
    quint8 checksum = 0xef ^ 1 ^ 2 ^ 3 ^ 4;
    while ((bytes.size() + 1) % 16) bytes += '\0';
    bytes += char(checksum);
    bytes += QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    return bytes;
}
#include <QTest>

static void acceptResetWarning(QObject *context) {
    QTimer::singleShot(0, context, [] {
        auto *confirmation = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        QVERIFY(confirmation);
        QCOMPARE(confirmation->objectName(), QString("esptoolResetConfirmation"));
        confirmation->button(QMessageBox::Ok)->click();
    });
}

class WindowTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QCoreApplication::setApplicationVersion("0.26.1");
    }
    void windowTitleIdentifiesRunningVersion() {
        AppConfig config; config.ini.path = "unused-smoke-config.ini";
        config.ini.profiles = {Profile{}};
        SessionWindow window(&config, config.ini.profiles.first());
        QVERIFY(window.windowTitle().contains("0.26.1"));
        QVERIFY(window.windowTitle().endsWith("ESP"));
        const auto *version = window.findChild<QLabel *>("applicationVersion");
        QVERIFY(version);
        QCOMPARE(version->text(), QString::fromUtf8("Serial ESP Helper — Version 0.26.1"));
    }
    void connectionSettingsFreezeUntilManualPause() {
        AppConfig config; config.ini.path = "unused-smoke-config.ini";
        config.ini.profiles = {Profile{}};
        SessionWindow window(&config, config.ini.profiles.first());
        const QStringList editors = {"profiles", "newProfile", "port", "baud",
                                      "forgetBinding", "connectButton"};
        for (const auto state : {SessionState::Waiting, SessionState::Connecting,
                                 SessionState::Connected, SessionState::Loaned}) {
            QVERIFY(QMetaObject::invokeMethod(&window, "applyState", Qt::DirectConnection, Q_ARG(SessionState, state)));
            for (const QString &name : editors) {
                const auto *editor = window.findChild<QWidget *>(name);
                QVERIFY2(editor, qPrintable(name)); QVERIFY2(!editor->isEnabled(), qPrintable(name));
            }
            QVERIFY(!window.findChild<QPushButton *>("sendButton")->isEnabled());
            QVERIFY(window.findChild<QCheckBox *>("readOnly")->isEnabled());
            QVERIFY(window.findChild<QPushButton *>("advanced")->isEnabled());
            QVERIFY(window.findChild<QCheckBox *>("autoReconnect")->isEnabled());
        }
        QVERIFY(QMetaObject::invokeMethod(&window, "applyState", Qt::DirectConnection,
                                         Q_ARG(SessionState, SessionState::ManuallyPaused)));
        for (const QString &name : editors) QVERIFY(window.findChild<QWidget *>(name)->isEnabled());
        QVERIFY(window.findChild<QCheckBox *>("readOnly")->isChecked());
    }
    void connectLocksImmediatelyWithoutOpeningUnknownPort() {
        AppConfig config; config.ini.path = "unused-smoke-config.ini";
        Profile p; p.port = "intentionally-absent-serial-test-port";
        config.ini.profiles = {p};
        SessionWindow window(&config, p);
        auto *connectButton = window.findChild<QPushButton *>("connectButton");
        QVERIFY(connectButton->isEnabled());
        connectButton->click();
        QVERIFY(!connectButton->isEnabled());
        QVERIFY(!window.findChild<QComboBox *>("port")->isEnabled());
        QVERIFY(!window.findChild<QComboBox *>("baud")->isEnabled());
    }
    void writableProfileEnablesSendOnlyWhenConnected() {
        AppConfig config; config.ini.path = "unused-smoke-config.ini";
        Profile p; p.readOnly = false; config.ini.profiles = {p};
        SessionWindow window(&config, p);
        auto *send = window.findChild<QPushButton *>("sendButton");
        QVERIFY(!send->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(&window, "applyState", Qt::DirectConnection,
                                         Q_ARG(SessionState, SessionState::Connected)));
        QVERIFY(send->isEnabled());
        auto *readOnly = window.findChild<QCheckBox *>("readOnly");
        readOnly->setChecked(true);
        QVERIFY(!send->isEnabled()); QVERIFY(!window.findChild<QComboBox *>("port")->isEnabled());
        readOnly->setChecked(false);
        QVERIFY(send->isEnabled()); QVERIFY(!window.findChild<QComboBox *>("port")->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(&window, "applyState", Qt::DirectConnection,
                                         Q_ARG(SessionState, SessionState::ManuallyPaused)));
        QVERIFY(!send->isEnabled());
    }
    void autoConnectRequiresExplicitProfileOptIn() {
        AppConfig config; config.ini.path = "unused-smoke-config.ini";
        Profile p; p.port = "intentionally-absent-serial-test-port"; config.ini.profiles = {p};
        SessionWindow manual(&config, p);
        QTest::qWait(50);
        QVERIFY(manual.findChild<QComboBox *>("port")->isEnabled());
        p.autoConnect = true;
        SessionWindow automatic(&config, p);
        QTRY_VERIFY_WITH_TIMEOUT(!automatic.findChild<QComboBox *>("port")->isEnabled(), 500);
        QVERIFY(manual.findChild<QComboBox *>("port")->isEnabled());
    }
    void esptoolUsesSeparateArgsAndReleasesPortOnFailureOrCancel() {
        QTemporaryDir directory(QDir::temp().filePath("esptool fixture-XXXXXX"));
        QVERIFY(directory.isValid());
        const QString fake = directory.filePath("official tool with spaces.exe");
        QVERIFY(QFile::copy(qEnvironmentVariable("FAKE_ESPTOOL_PATH"), fake));
        const QString argsFile = directory.filePath("arguments.jsonl");
        qputenv("FAKE_ESPTOOL_ARGS_FILE", argsFile.toUtf8());
        qunsetenv("FAKE_ESPTOOL_MODE");

        Profile profile;
        profile.port = "COM port 7";
        profile.esptoolPath = fake;
        int acquired = 0, released = 0;
        EsptoolDialog dialog(profile, [&acquired](QString &) { ++acquired; return true; },
                             [&released] { ++released; });
        auto *status = dialog.findChild<QLabel *>("esptoolStatus");
        auto *version = dialog.findChild<QPushButton *>("checkEsptoolVersion");
        auto *chip = dialog.findChild<QPushButton *>("chipFlashInfo");
        auto *cancel = dialog.findChild<QPushButton *>("cancelOperation");
        QVERIFY(status && version && chip && cancel);

        version->click();
        QTRY_VERIFY_WITH_TIMEOUT(status->text().contains("esptool v5.1.0"), 5000);
        QCOMPARE(acquired, 0);

        acceptResetWarning(&dialog);
        chip->click();
        QTRY_VERIFY_WITH_TIMEOUT(status->text().contains("finished")
                                 || status->text().contains("failed"), 5000);
        QVERIFY2(status->text().contains("finished"),
            qPrintable(status->text() + "\n"
                + dialog.findChild<QPlainTextEdit *>("esptoolOutput")->toPlainText()));
        QCOMPARE(acquired, 1);
        QCOMPARE(released, 1);
        QFile recorded(argsFile);
        QVERIFY(recorded.open(QIODevice::ReadOnly));
        const auto invocations = recorded.readAll().split('\n');
        QVERIFY(invocations.size() >= 5);
        const auto chipArgs = QJsonDocument::fromJson(invocations[2]).array();
        QVERIFY(chipArgs.contains("COM port 7"));
        QVERIFY(chipArgs.contains("chip-id"));
        QVERIFY(chipArgs.contains("--port"));
        const auto flashArgs = QJsonDocument::fromJson(invocations[3]).array();
        QVERIFY(flashArgs.contains("flash-id"));

        qputenv("FAKE_ESPTOOL_MODE", "fail");
        acceptResetWarning(&dialog);
        chip->click();
        QTRY_VERIFY_WITH_TIMEOUT(status->text().contains("failed"), 5000);
        QCOMPARE(acquired, 2);
        QCOMPARE(released, 2);

        qputenv("FAKE_ESPTOOL_MODE", "sleep");
        acceptResetWarning(&dialog);
        chip->click();
        QTRY_VERIFY_WITH_TIMEOUT(status->text().contains("Running chip-id"), 5000);
        cancel->click();
        QTRY_VERIFY_WITH_TIMEOUT(status->text().contains("failed"), 5000);
        QCOMPARE(acquired, 3);
        QCOMPARE(released, 3);
        qunsetenv("FAKE_ESPTOOL_MODE");
        qunsetenv("FAKE_ESPTOOL_ARGS_FILE");
    }
    void manualPauseCancelsAnActiveEsptoolHandoff() {
        qputenv("FAKE_ESPTOOL_MODE", "sleep");
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AppConfig config; config.ini.path = directory.filePath("settings.ini");
        Profile profile;
        profile.port = "TEST";
        profile.esptoolPath = qEnvironmentVariable("FAKE_ESPTOOL_PATH");
        config.ini.profiles = {profile};
        auto *serial = new WindowSerial;
        SessionWindow window(&config, profile, serial);
        window.findChild<QPushButton *>("connectButton")->click();
        QTRY_COMPARE_WITH_TIMEOUT(window.findChild<QLabel *>("sessionState")->text(),
                                  QString("Connected"), 2000);
        window.findChild<QPushButton *>("esptoolDiagnostics")->click();
        auto *dialog = window.findChild<EsptoolDialog *>();
        QVERIFY(dialog);
        acceptResetWarning(dialog);
        dialog->findChild<QPushButton *>("chipFlashInfo")->click();
        QTRY_VERIFY_WITH_TIMEOUT(dialog->findChild<QLabel *>("esptoolStatus")
            ->text().contains("Running chip-id"), 5000);
        window.findChild<QPushButton *>("pauseButton")->click();
        QTRY_COMPARE_WITH_TIMEOUT(window.findChild<QLabel *>("sessionState")->text(),
                                  QString("Manually paused"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(dialog->findChild<QLabel *>("esptoolStatus")
            ->text().contains("failed"), 5000);
        QVERIFY(!serial->opened);
        qunsetenv("FAKE_ESPTOOL_MODE");
    }
    void diagnosticsRequireResetConfirmation_data() {
        QTest::addColumn<QString>("action");
        QTest::newRow("chip-and-flash") << QString("chipFlashInfo");
        QTest::newRow("partition-table") << QString("readPartitionTable");
        QTest::newRow("layout-and-ota") << QString("readOtaInfo");
    }
    void diagnosticsRequireResetConfirmation() {
        QFETCH(QString, action);
        Profile profile;
        profile.port = "TEST";
        profile.esptoolPath = qEnvironmentVariable("FAKE_ESPTOOL_PATH");
        int acquired = 0, released = 0;
        EsptoolDialog dialog(profile, [&acquired](QString &) { ++acquired; return true; },
                             [&released] { ++released; });
        auto *output = dialog.findChild<QPlainTextEdit *>("esptoolOutput");
        auto *button = dialog.findChild<QPushButton *>(action);
        QVERIFY(output && button);
        for (int attempt = 0; attempt < 2; ++attempt) {
            QTimer::singleShot(0, &dialog, [&] {
                auto *confirmation = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                QVERIFY(confirmation);
                QCOMPARE(confirmation->objectName(), QString("esptoolResetConfirmation"));
                QCOMPARE(confirmation->defaultButton(), confirmation->button(QMessageBox::Cancel));
                QVERIFY(confirmation->text().contains("unsaved runtime state"));
                QVERIFY(confirmation->text().contains("diagnostic reads can reset"));
                if (action == "chipFlashInfo")
                    QVERIFY(confirmation->informativeText().contains("two commands"));
                QCOMPARE(acquired, 0);
                QVERIFY(output->toPlainText().isEmpty());
                QTest::keyClick(confirmation, Qt::Key_Escape);
            });
            button->click();
            QCOMPARE(acquired, 0);
            QCOMPARE(released, 0);
            QVERIFY(output->toPlainText().isEmpty());
            QVERIFY(button->isEnabled());
        }
    }
    void resetConfirmationCanBeDisabledWithoutChangingOperationBehavior() {
        Profile profile;
        profile.port = "TEST";
        profile.esptoolPath = qEnvironmentVariable("FAKE_ESPTOOL_PATH");
        int acquired = 0, released = 0;
        EsptoolDialog dialog(profile, [&acquired](QString &) { ++acquired; return true; },
                             [&released] { ++released; }, false);
        auto *status = dialog.findChild<QLabel *>("esptoolStatus");
        dialog.findChild<QPushButton *>("chipFlashInfo")->click();
        QTRY_VERIFY_WITH_TIMEOUT(status->text().contains("finished"), 5000);
        QVERIFY(!QApplication::activeModalWidget());
        QCOMPARE(acquired, 1);
        QCOMPARE(released, 1);
    }
    void rawDumpConfirmationCancelsBeforeProcessAndAllowsConfirmedRead() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString destination = directory.filePath("raw dump.bin");
        Profile profile;
        profile.port = "TEST";
        profile.esptoolPath = qEnvironmentVariable("FAKE_ESPTOOL_PATH");
        int acquired = 0, released = 0;
        EsptoolDialog dialog(profile, [&acquired](QString &) { ++acquired; return true; },
                             [&released] { ++released; });
        auto *output = dialog.findChild<QPlainTextEdit *>("esptoolOutput");
        auto *status = dialog.findChild<QLabel *>("esptoolStatus");
        for (bool proceed : {false, true}) {
            QTimer::singleShot(0, &dialog, [&, proceed] {
                auto *address = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
                QVERIFY(address);
                address->setTextValue("0x10");
                QTimer::singleShot(0, &dialog, [&, proceed] {
                    auto *size = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
                    QVERIFY(size);
                    size->setTextValue("16");
                    QTimer::singleShot(0, &dialog, [&, proceed] {
                        auto *save = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
                        QVERIFY(save);
                        save->selectFile(destination);
                        QTimer::singleShot(0, &dialog, [&, proceed] {
                            auto *confirmation = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                            QVERIFY(confirmation);
                            QCOMPARE(confirmation->objectName(), QString("esptoolResetConfirmation"));
                            QCOMPARE(acquired, 0);
                            QVERIFY(output->toPlainText().isEmpty());
                            confirmation->button(proceed ? QMessageBox::Ok : QMessageBox::Cancel)->click();
                        });
                        QMetaObject::invokeMethod(save, "accept", Qt::DirectConnection);
                    });
                    size->accept();
                });
                address->accept();
            });
            dialog.findChild<QPushButton *>("readRawFlash")->click();
            if (!proceed) {
                QCOMPARE(acquired, 0);
                QCOMPARE(released, 0);
                QVERIFY(output->toPlainText().isEmpty());
                QVERIFY(!QFileInfo::exists(destination));
                QVERIFY(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
            } else {
                QTRY_VERIFY_WITH_TIMEOUT(status->text().startsWith("Saved 16 exact bytes"), 5000);
                QCOMPARE(acquired, 1);
                QCOMPARE(released, 1);
                QFile dump(destination);
                QVERIFY(dump.open(QIODevice::ReadOnly));
                QCOMPARE(dump.readAll(), QByteArray(16, char(0xa5)));
            }
        }
    }
    void layoutAndOtaReadsUseExplicitAddressesAndClearStaleSelection() {
        QTemporaryDir directory;
        const QString argsFile = directory.filePath("read-args.jsonl");
        qputenv("FAKE_ESPTOOL_ARGS_FILE", argsFile.toUtf8());
        qputenv("FAKE_ESPTOOL_MODE", "ota");
        Profile profile;
        profile.port = "TEST";
        profile.esptoolPath = qEnvironmentVariable("FAKE_ESPTOOL_PATH");
        int acquired = 0, released = 0;
        EsptoolDialog dialog(profile, [&acquired](QString &) { ++acquired; return true; },
                             [&released] { ++released; });
        auto *offset = dialog.findChild<QLineEdit *>("partitionTableOffset");
        auto *button = dialog.findChild<QPushButton *>("readOtaInfo");
        auto *status = dialog.findChild<QLabel *>("esptoolStatus");
        auto *ota = dialog.findChild<QLabel *>("otaSelectionStatus");
        auto *partitions = dialog.findChild<QComboBox *>("esptoolPartitions");
        offset->setText("0xa000");
        acceptResetWarning(&dialog);
        button->click();
        QTRY_VERIFY_WITH_TIMEOUT(button->isEnabled(), 5000);
        QVERIFY2(status->text().contains("snapshot read"), qPrintable(status->text()));
        QCOMPARE(acquired, 1);
        QCOMPARE(released, 1);
        QCOMPARE(partitions->count(), 3);
        QVERIFY(partitions->itemText(1).contains("app/ota-0"));
        QVERIFY(partitions->itemText(2).contains("app/ota-1"));
        QVERIFY(partitions->itemText(2).contains("offset 0x20000, size 0x10000"));
        QVERIFY(ota->text().contains("candidate: OTA1"));
        QVERIFY(ota->text().contains("NOT proof"));
        QVERIFY(ota->text().contains("No partition is automatically"));
        QFile record(argsFile);
        QVERIFY(record.open(QIODevice::ReadOnly));
        const auto calls = record.readAll().split('\n');
        QCOMPARE(calls.size(), 4);
        const auto tableRead = QJsonDocument::fromJson(calls[1]).array();
        QVERIFY(tableRead.contains("read-flash"));
        QVERIFY(tableRead.contains("0xa000"));
        QVERIFY(tableRead.contains("0xC00"));
        const auto otaRead = QJsonDocument::fromJson(calls[2]).array();
        QVERIFY(otaRead.contains("0xd000"));
        QVERIFY(otaRead.contains("0x2000"));

        for (const auto &mode : {"ota-bad-crc", "ota-encrypted", "ota-ambiguous",
                                 "ota-short", "ota-read-fail"}) {
            qputenv("FAKE_ESPTOOL_MODE", mode);
            acceptResetWarning(&dialog);
            button->click();
            QVERIFY(!ota->text().contains("candidate:"));
            QTRY_VERIFY_WITH_TIMEOUT(button->isEnabled(), 5000);
            QVERIFY2(!ota->text().contains("candidate:"), mode);
            QVERIFY(ota->text().contains("UNKNOWN") || ota->text().contains("No usable"));
        }
        QCOMPARE(acquired, 6);
        QCOMPARE(released, 6);
        offset->setText("0x8000");
        QCOMPARE(partitions->count(), 0);
        qunsetenv("FAKE_ESPTOOL_MODE");
        qunsetenv("FAKE_ESPTOOL_ARGS_FILE");
    }

    void offlinePlanNeedsNoToolPortOrResetAndInvalidatesEdits() {
        Profile profile;
        int acquired = 0;
        EsptoolDialog diagnostics(profile, [&acquired](QString &) { ++acquired; return true; }, [] {});
        auto *open = diagnostics.findChild<QPushButton *>("openFlashPlan");
        QVERIFY(open->isEnabled());
        open->click();
        auto *plan = diagnostics.findChild<FlashPlanDialog *>();
        QVERIFY(plan);
        QVERIFY(!QApplication::activeModalWidget());
        QCOMPARE(acquired, 0);
        auto *preview = plan->findChild<QPushButton *>("inspectFlashPlan");
        auto *recheck = plan->findChild<QPushButton *>("recheckFlashPlan");
        auto *report = plan->findChild<QPlainTextEdit *>("flashPlanReport");
        QVERIFY(!recheck->isEnabled());
        preview->click();
        QVERIFY(report->toPlainText().contains("PLAN REJECTED"));
        plan->findChild<QComboBox *>("planCapacity")->setEditText("0x400000");
        QVERIFY(report->toPlainText().contains("Inspect the plan again"));
        QVERIFY(!recheck->isEnabled());
        QTemporaryDir directory;
        const QString dataPath = directory.filePath("data.bin");
        QFile data(dataPath);
        QVERIFY(data.open(QIODevice::WriteOnly));
        QCOMPARE(data.write("opaque data"), qint64(11));
        data.close();
        auto *chip = plan->findChild<QComboBox *>("planChip");
        chip->setCurrentIndex(chip->findData("esp32s3"));
        QTimer::singleShot(0, plan, [&] {
            auto *file = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            QVERIFY(file);
            file->selectFile(dataPath);
            QMetaObject::invokeMethod(file, "accept", Qt::DirectConnection);
        });
        plan->findChild<QPushButton *>("addFlashFile")->click();
        preview->click();
        QVERIFY2(report->toPlainText().contains("not authorization"), qPrintable(report->toPlainText()));
        QVERIFY(recheck->isEnabled());
        QVERIFY(plan->findChild<QPushButton *>("writeFlashPlan")->isEnabled());
        recheck->click();
        QVERIFY(report->toPlainText().contains("unchanged"));
        QVERIFY(data.open(QIODevice::WriteOnly));
        QCOMPARE(data.write("changed data"), qint64(12));
        data.close();
        recheck->click();
        QVERIFY(report->toPlainText().contains("PLAN INVALIDATED"));
        QVERIFY(!recheck->isEnabled());
        preview->click();
        QVERIFY(recheck->isEnabled());
        const QString manifestPath = directory.filePath("broken.json");
        QFile manifest(manifestPath);
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        manifest.write("{broken");
        manifest.close();
        QTimer::singleShot(0, plan, [&] {
            auto *file = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            QVERIFY(file);
            file->selectFile(manifestPath);
            QMetaObject::invokeMethod(file, "accept", Qt::DirectConnection);
        });
        plan->findChild<QPushButton *>("loadFlashManifest")->click();
        QVERIFY(report->toPlainText().contains("MANIFEST REJECTED"));
        QVERIFY(!recheck->isEnabled());
        QCOMPARE(acquired, 0);
        plan->close();
    }
    void flashPlanReusesChipInfoAndDropsDefaultsAfterFailedRefresh() {
        Profile profile;
        profile.port = "TEST";
        profile.esptoolPath = qEnvironmentVariable("FAKE_ESPTOOL_PATH");
        EsptoolDialog diagnostics(profile, [](QString &) { return true; }, [] {}, false);
        auto *status = diagnostics.findChild<QLabel *>("esptoolStatus");
        auto *read = diagnostics.findChild<QPushButton *>("chipFlashInfo");
        auto *open = diagnostics.findChild<QPushButton *>("openFlashPlan");
        read->click();
        QTRY_VERIFY_WITH_TIMEOUT(read->isEnabled(), 5000);
        QVERIFY(status->text().contains("finished"));
        open->click();
        auto *plan = diagnostics.findChild<FlashPlanDialog *>();
        QVERIFY(plan);
        QCOMPARE(plan->findChild<QComboBox *>("planChip")->currentData().toString(), QString("esp32s3"));
        QCOMPARE(plan->findChild<QComboBox *>("planCapacity")->currentData().toULongLong(),
                 quint64(0x400000));
        QVERIFY(plan->findChild<QLabel *>("planDefaultsSource")->text().contains("checked again"));
        plan->close();
        QTRY_VERIFY(diagnostics.findChild<FlashPlanDialog *>() == nullptr);
        qputenv("FAKE_ESPTOOL_MODE", "fail");
        read->click();
        QTRY_VERIFY_WITH_TIMEOUT(read->isEnabled(), 5000);
        QVERIFY(status->text().contains("failed"));
        open->click();
        plan = diagnostics.findChild<FlashPlanDialog *>();
        QVERIFY(plan);
        QVERIFY(plan->findChild<QComboBox *>("planChip")->currentData().toString().isEmpty());
        QVERIFY(!plan->findChild<QComboBox *>("planCapacity")->currentData().isValid());
        plan->close();
        qunsetenv("FAKE_ESPTOOL_MODE");
    }
    void mergedFlashUsesCommonDropdownPresets_data() {
        QTest::addColumn<QString>("chip");
        QTest::addColumn<quint64>("headerOffset");
        QTest::addColumn<quint16>("chipId");
        QTest::newRow("esp32s3") << QString("esp32s3") << quint64(0) << quint16(9);
        QTest::newRow("esp32") << QString("esp32") << quint64(0x1000) << quint16(0);
    }
    void mergedFlashUsesCommonDropdownPresets() {
        QFETCH(QString, chip);
        QFETCH(quint64, headerOffset);
        QFETCH(quint16, chipId);
        QTemporaryDir directory;
        const QString path = directory.filePath("merged.bin");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray bytes = QByteArray(int(headerOffset), char(0xff)) + sampleEsp32S3App(chipId);
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
        file.close();
        FlashPlan::Plan captured;
        FlashPlanDialog plan({}, [&captured](const FlashPlan::Plan &value) { captured = value; },
                             nullptr, chip, 0x400000);
        auto *capacity = plan.findChild<QComboBox *>("planCapacity");
        QVERIFY(capacity->isEditable());
        QVERIFY(capacity->findData(QVariant::fromValue(quint64(0x800000))) >= 0);
        QTimer::singleShot(0, &plan, [&] {
            auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->selectFile(path);
            QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
        });
        plan.findChild<QPushButton *>("addMergedFile")->click();
        auto *table = plan.findChild<QTableWidget *>("planInputs");
        QCOMPARE(table->rowCount(), 1);
        auto *address = qobject_cast<QComboBox *>(table->cellWidget(0, 2));
        auto *header = qobject_cast<QComboBox *>(table->cellWidget(0, 3));
        QVERIFY(address && header);
        QCOMPARE(address->currentData().toULongLong(), quint64(0));
        QCOMPARE(header->currentData().toULongLong(), headerOffset);
        plan.findChild<QPushButton *>("inspectFlashPlan")->click();
        auto *write = plan.findChild<QPushButton *>("writeFlashPlan");
        QVERIFY2(write->isEnabled(),
                 qPrintable(plan.findChild<QPlainTextEdit *>("flashPlanReport")->toPlainText()));
        write->click();
        QCOMPARE(captured.chip, chip);
        QCOMPARE(captured.flashSize, quint64(0x400000));
        QCOMPARE(captured.files.size(), 1);
        QCOMPARE(captured.files[0].input.address, quint64(0));
        QCOMPARE(captured.files[0].input.imageOffset, headerOffset);
        address->setEditText("0x2000");
        QVERIFY(!write->isEnabled());
        plan.findChild<QPushButton *>("inspectFlashPlan")->click();
        QVERIFY(write->isEnabled());
        write->click();
        QCOMPARE(captured.files[0].input.address, quint64(0x2000));
    }
    void appTargetDropdownFillsPartitionAddress() {
        const QVector<Esptool::Partition> partitions{
            {0, 0x10, 0x10000, 0x10000, "ota0", 0},
            {0, 0x11, 0x20000, 0x10000, "ota1", 0}};
        FlashPlan::Plan captured;
        FlashPlanDialog plan(partitions,
            [&captured](const FlashPlan::Plan &value) { captured = value; }, nullptr, "esp32s3", 0x400000);
        QTemporaryDir directory;
        const QString path = directory.filePath("app.bin");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const auto bytes = sampleEsp32S3App();
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
        file.close();
        QTimer::singleShot(0, &plan, [&] {
            auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->selectFile(path);
            QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
        });
        plan.findChild<QPushButton *>("addFlashFile")->click();
        auto *table = plan.findChild<QTableWidget *>("planInputs");
        auto *role = qobject_cast<QComboBox *>(table->cellWidget(0, 1));
        auto *address = qobject_cast<QComboBox *>(table->cellWidget(0, 2));
        auto *header = qobject_cast<QComboBox *>(table->cellWidget(0, 3));
        role->setCurrentIndex(role->findData(int(FlashPlan::Kind::App)));
        QVERIFY(!header->isEnabled());
        auto *target = plan.findChild<QComboBox *>("planAppTarget");
        target->setCurrentIndex(target->findData(1));
        QCOMPARE(address->currentData().toULongLong(), quint64(0x20000));
        QVERIFY(!address->isEnabled());
        plan.findChild<QPushButton *>("inspectFlashPlan")->click();
        auto *write = plan.findChild<QPushButton *>("writeFlashPlan");
        QVERIFY(write->isEnabled());
        write->click();
        QCOMPARE(captured.files.first().input.address, quint64(0x20000));
        target->setCurrentIndex(target->findData(-2));
        QVERIFY(!write->isEnabled());
        plan.findChild<QPushButton *>("inspectFlashPlan")->click();
        QVERIFY(write->isEnabled());
        write->click();
        QCOMPARE(captured.files.size(), 2);
        target->setCurrentIndex(target->findData(-1));
        QVERIFY(address->isEnabled());
    }
    void explicitBothOtaSlotsAreVerifiedAndWrittenWithoutBootSelectionChange() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString argsFile = directory.filePath("flash-args.jsonl");
        const QString appPath = directory.filePath("firmware.bin");
        QFile app(appPath);
        QVERIFY(app.open(QIODevice::WriteOnly));
        QCOMPARE(app.write(sampleEsp32S3App()), qint64(sampleEsp32S3App().size()));
        app.close();
        qputenv("FAKE_ESPTOOL_ARGS_FILE", argsFile.toUtf8());
        qputenv("FAKE_ESPTOOL_MODE", "ota");
        Profile profile;
        profile.port = "TEST";
        profile.esptoolPath = qEnvironmentVariable("FAKE_ESPTOOL_PATH");
        int acquired = 0, released = 0;
        EsptoolDialog diagnostics(profile, [&acquired](QString &) { ++acquired; return true; },
                                  [&released] { ++released; });
        auto *status = diagnostics.findChild<QLabel *>("esptoolStatus");
        acceptResetWarning(&diagnostics);
        diagnostics.findChild<QPushButton *>("readOtaInfo")->click();
        QTRY_VERIFY_WITH_TIMEOUT(status->text().contains("snapshot read"), 5000);
        QCOMPARE(acquired, 1);
        QCOMPARE(released, 1);

        diagnostics.findChild<QPushButton *>("openFlashPlan")->click();
        auto *planDialog = diagnostics.findChild<FlashPlanDialog *>();
        QVERIFY(planDialog);
        planDialog->findChild<QComboBox *>("planChip")
            ->setCurrentIndex(planDialog->findChild<QComboBox *>("planChip")->findData("esp32s3"));
        planDialog->findChild<QComboBox *>("planCapacity")->setEditText("0x400000");
        QTimer::singleShot(0, planDialog, [&, planDialog] {
            auto *file = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            QVERIFY(file);
            file->selectFile(appPath);
            QMetaObject::invokeMethod(file, "accept", Qt::DirectConnection);
        });
        planDialog->findChild<QPushButton *>("addFlashFile")->click();
        auto *table = planDialog->findChild<QTableWidget *>("planInputs");
        QCOMPARE(table->rowCount(), 1);
        auto *kind = qobject_cast<QComboBox *>(table->cellWidget(0, 1));
        QVERIFY(kind);
        kind->setCurrentIndex(kind->findData(int(FlashPlan::Kind::App)));
        auto *target = planDialog->findChild<QComboBox *>("planAppTarget");
        target->setCurrentIndex(target->findData(-2));
        auto *report = planDialog->findChild<QPlainTextEdit *>("flashPlanReport");
        planDialog->findChild<QPushButton *>("inspectFlashPlan")->click();
        QVERIFY2(report->toPlainText().contains("0x10000"), qPrintable(report->toPlainText()));
        QVERIFY(report->toPlainText().contains("0x20000"));

        QTimer confirmationTimer;
        confirmationTimer.setInterval(5);
        bool writeOperationStarted = false;
        bool sawFinalConfirmation = false;
        connect(&confirmationTimer, &QTimer::timeout, &diagnostics, [&] {
            if (status->text().startsWith("Checking esptool") || status->text().startsWith("Running "))
                writeOperationStarted = true;
            if (writeOperationStarted
                && (status->text().startsWith("Flash completed") || status->text().contains("failed"))) {
                confirmationTimer.stop();
                return;
            }
            auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!message) return;
            if (message->objectName() == "esptoolResetConfirmation")
                message->button(QMessageBox::Ok)->click();
            else if (message->objectName() == "flashRoleConfirmation")
                message->button(QMessageBox::Yes)->click();
            else if (message->objectName() == "finalFlashConfirmation") {
                sawFinalConfirmation = true;
                QCOMPARE(message->defaultButton(), message->button(QMessageBox::Cancel));
                message->button(QMessageBox::Yes)->click();
            }
        });
        confirmationTimer.start();
        planDialog->findChild<QPushButton *>("writeFlashPlan")->click();
        QTRY_VERIFY_WITH_TIMEOUT(status->text().startsWith("Flash completed"), 10000);
        QVERIFY(sawFinalConfirmation);
        QCOMPARE(acquired, 2);
        QCOMPARE(released, 2);
        QFile recorded(argsFile);
        QVERIFY(recorded.open(QIODevice::ReadOnly));
        int writes = 0;
        bool wroteOta0 = false, wroteOta1 = false;
        for (const auto &line : recorded.readAll().split('\n')) {
            const auto args = QJsonDocument::fromJson(line).array();
            if (!args.contains("write-flash")) continue;
            ++writes;
            QVERIFY(args.contains(appPath));
            QVERIFY(args.contains("--flash-size"));
            wroteOta0 = wroteOta0 || args.contains("0x10000");
            wroteOta1 = wroteOta1 || args.contains("0x20000");
        }
        QCOMPARE(writes, 1);
        QVERIFY(wroteOta0 && wroteOta1);
        QVERIFY(status->text().contains("OTA boot selection unchanged"));

        qputenv("FAKE_ESPTOOL_MODE", "wrong-chip");
        writeOperationStarted = false;
        confirmationTimer.start();
        planDialog->findChild<QPushButton *>("writeFlashPlan")->click();
        QTRY_VERIFY_WITH_TIMEOUT(status->text().contains("WRONG CHIP"), 10000);
        confirmationTimer.stop();
        QCOMPARE(acquired, 3);
        QCOMPARE(released, 3);
        QFile noWrongChipWrite(argsFile);
        QVERIFY(noWrongChipWrite.open(QIODevice::ReadOnly));
        int writesAfterMismatch = 0;
        for (const auto &line : noWrongChipWrite.readAll().split('\n'))
            if (QJsonDocument::fromJson(line).array().contains("write-flash")) ++writesAfterMismatch;
        QCOMPARE(writesAfterMismatch, 1);
        planDialog->close();
        qunsetenv("FAKE_ESPTOOL_MODE");
        qunsetenv("FAKE_ESPTOOL_ARGS_FILE");
    }
    void loggingSettingsAreVisibleEditableAndPersisted() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AppConfig config; QString error;
        QVERIFY(config.ini.load(directory.filePath("settings.ini"), error));
        SessionWindow window(&config, config.ini.profiles.first());
        QVERIFY(window.findChild<QLabel *>("logStatus")->text().contains("Traffic logging OFF"));
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QDialog *>();
            QVERIFY(dialog);
            QCOMPARE(dialog->findChild<QTabWidget *>()->currentIndex(), 1);
            dialog->findChild<QCheckBox *>("logging")->setChecked(true);
            dialog->findChild<QCheckBox *>("newLogOnReconnect")->setChecked(true);
            dialog->findChild<QLineEdit *>("logDirectory")->setText(directory.path());
            dialog->findChild<QLineEdit *>("logName")->setText("{profile}-{date}");
            dialog->findChild<QLineEdit *>("logSeconds")->setText("60");
            dialog->findChild<QLineEdit *>("terminalRows")->setText("321");
            dialog->findChild<QLineEdit *>("displayQueueBytes")->setText("4096");
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        window.findChild<QPushButton *>("logSettings")->click();
        window.findChild<QCheckBox *>("autoReconnect")->setChecked(false);
        window.findChild<QPushButton *>("saveProfile")->click();
        IniFile restored;
        QVERIFY2(restored.load(config.ini.path, error), qPrintable(error));
        const auto p = restored.profiles.first();
        QVERIFY(p.logging); QVERIFY(p.newLogOnReconnect); QVERIFY(!p.autoReconnect);
        QCOMPARE(p.logDirectory, directory.path());
        QCOMPARE(p.logName, QString("{profile}-{date}"));
        QCOMPARE(p.logSeconds, 60); QCOMPARE(p.terminalRows, 321);
        QCOMPARE(p.displayQueueBytes, 4096);
        QVERIFY(window.findChild<QLabel *>("logStatus")->text().contains(directory.path()));
    }
    void terminalExportControlsReflectSelectionAndFilter() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        AppConfig config; config.ini.path = directory.filePath("settings.ini");
        config.ini.profiles = {Profile{}};
        SessionWindow window(&config, config.ini.profiles.first());
        QVERIFY(window.findChild<QPushButton *>("saveBuffer")->isEnabled());
        auto *selection = window.findChild<QPushButton *>("saveSelection");
        auto *filtered = window.findChild<QPushButton *>("saveFiltered");
        QVERIFY(!selection->isEnabled()); QVERIFY(!filtered->isEnabled());
        window.findChild<QLineEdit *>("terminalFilter")->setText("error");
        QVERIFY(filtered->isEnabled());
        window.findChild<QLineEdit *>("terminalFilter")->clear();
        QVERIFY(!filtered->isEnabled());
        auto *terminal = window.findChild<QTextEdit *>("terminal");
        terminal->setPlainText("first\nsecond");
        auto cursor = terminal->textCursor();
        cursor.setPosition(1); cursor.setPosition(9, QTextCursor::KeepAnchor);
        terminal->setTextCursor(cursor);
        QVERIFY(selection->isEnabled());
        const auto output = directory.filePath("selection.txt");
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QFileDialog *>();
            QVERIFY(dialog);
            dialog->selectFile(output);
            QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
        });
        selection->click();
        QFile saved(output); QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), QByteArray("irst\nsec"));
        cursor.clearSelection(); terminal->setTextCursor(cursor);
        QVERIFY(!selection->isEnabled());
    }
    void eachSettingsTabSavesAllFieldsAndCancelDoesNotSave() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        AppConfig config; QString error;
        QVERIFY(config.ini.load(directory.filePath("settings.ini"), error));
        SessionWindow window(&config, config.ini.profiles.first());
        for (int tab = 0; tab < 3; ++tab) {
            QTimer::singleShot(0, &window, [&, tab] {
                auto *dialog = window.findChild<QDialog *>();
                QVERIFY(dialog);
                QVERIFY(dialog->findChild<QLabel *>("settingsScope")->text().contains("GLOBAL fields"));
                for (int page = 0; page < 3; ++page)
                    QVERIFY(dialog->findChild<QPushButton *>(QString("saveSettings%1").arg(page)));
                dialog->findChild<QTabWidget *>()->setCurrentIndex(tab);
                dialog->findChild<QLineEdit *>("esptoolPath")->setText("C:/Tools/esptool.exe");
                dialog->findChild<QLineEdit *>("logExtension")->setText(".bin");
                dialog->findChild<QLineEdit *>("logName")->setText("capture");
                auto *mode = dialog->findChild<QComboBox *>("logMode");
                mode->setCurrentText("text");
                QVERIFY(!dialog->findChild<QLineEdit *>("rawLogExtension")->isEnabled());
                QVERIFY(dialog->findChild<QLineEdit *>("logExtension")->isEnabled());
                mode->setCurrentText("raw");
                QVERIFY(dialog->findChild<QLineEdit *>("rawLogExtension")->isEnabled());
                QVERIFY(!dialog->findChild<QLineEdit *>("logExtension")->isEnabled());
                mode->setCurrentText("both");
                dialog->findChild<QCheckBox *>("textTimestamp")->setChecked(true);
                dialog->findChild<QCheckBox *>("terminalTimestamp")->setChecked(true);
                dialog->findChild<QCheckBox *>("timestampMilliseconds")->setChecked(true);
                dialog->findChild<QPushButton *>(QString("saveSettings%1").arg(tab))->click();
            });
            window.findChild<QPushButton *>("advanced")->click();
            IniFile restored; QVERIFY(restored.load(config.ini.path, error));
            const auto &p = restored.profiles.first();
            QCOMPARE(p.esptoolPath, QString("C:/Tools/esptool.exe"));
            QCOMPARE(p.logExtension, QString(".bin"));
            QCOMPARE(p.logName, QString("capture"));
            QVERIFY(p.textLogging() && p.rawLogging() && p.textTimestamp
                && p.terminalTimestamp && p.timestampMilliseconds);
        }
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QDialog *>();
            dialog->findChild<QLineEdit *>("esptoolPath")->setText("not-saved.exe");
            dialog->reject();
        });
        window.findChild<QPushButton *>("advanced")->click();
        QCOMPARE(config.ini.profiles.first().esptoolPath, QString("C:/Tools/esptool.exe"));
    }
    void diagnosticsSavePathSurvivesReload() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        AppConfig config; QString error;
        QVERIFY(config.ini.load(directory.filePath("settings.ini"), error));
        SessionWindow window(&config, config.ini.profiles.first());
        window.findChild<QPushButton *>("esptoolDiagnostics")->click();
        auto *dialog = window.findChild<EsptoolDialog *>();
        QVERIFY(dialog);
        dialog->findChild<QLineEdit *>("esptoolPath")->setText("C:/Tools/esptool.exe");
        QVERIFY(dialog->findChild<QLabel *>("esptoolSettingsScope")->text().contains("NOT saved"));
        dialog->findChild<QPushButton *>("saveEsptoolSettings")->click();
        QVERIFY(dialog->findChild<QLabel *>("esptoolSettingsScope")->text().contains("GLOBAL esptool path saved"));
        IniFile restored;
        QVERIFY(restored.load(config.ini.path, error));
        QCOMPARE(restored.esptoolPath, QString("C:/Tools/esptool.exe"));
        dialog->close();
    }
    void terminalTimestampsPreserveFilteringAndRawLog() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        AppConfig config; config.ini.path = directory.filePath("settings.ini");
        Profile p; p.port = "TEST"; p.terminalTimestamp = p.timestampMilliseconds = true;
        p.logMode = "raw";
        p.logDirectory = directory.path(); p.logging = true;
        config.ini.profiles = {p};
        auto *serial = new WindowSerial;
        SessionWindow window(&config, p, serial);
        window.findChild<QPushButton *>("connectButton")->click();
        QTRY_COMPARE(window.findChild<QLabel *>("sessionState")->text(), QString("Connected"));
        QMetaObject::invokeMethod(serial, [serial] { emit serial->received("info\nerror temp=23\n"); },
                                  Qt::QueuedConnection);
        auto *view = window.findChild<QTextEdit *>("terminal");
        QTRY_VERIFY(view->toPlainText().contains("UTC] error temp=23"));
        window.findChild<QLineEdit *>("terminalFilter")->setText("error");
        QVERIFY(view->toPlainText().contains("UTC] error temp=23"));
        QVERIFY(!view->toPlainText().contains("info"));
        window.findChild<QPushButton *>("pauseButton")->click();
        QTRY_COMPARE(window.findChild<QLabel *>("sessionState")->text(), QString("Manually paused"));
        const auto logs = QDir(directory.path()).entryList({"*.raw"}, QDir::Files);
        QCOMPARE(logs.size(), 1);
        QFile file(directory.filePath(logs.first())); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("info\nerror temp=23\n"));
    }
    void expressionViewHighlightExportAndProfile() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        AppConfig config; QString error;
        QVERIFY(config.ini.load(directory.filePath("settings.ini"), error));
        Profile p; p.port = "TEST"; p.logDirectory = directory.path();
        config.ini.profiles = {p};
        auto *serial = new WindowSerial;
        SessionWindow window(&config, p, serial);
        window.findChild<QPushButton *>("connectButton")->click();
        QTRY_COMPARE_WITH_TIMEOUT(window.findChild<QLabel *>("sessionState")->text(), QString("Connected"), 2000);
        QMetaObject::invokeMethod(serial, [serial] {
            emit serial->received("info temp=1\nerror flash temp=23\nerror timeout temp=5\n");
        }, Qt::QueuedConnection);
        auto *terminal = window.findChild<QTextEdit *>("terminal");
        QTRY_VERIFY(terminal->toPlainText().contains("error flash temp=23"));
        auto *mode = window.findChild<QComboBox *>("filterMode");
        auto *query = window.findChild<QLineEdit *>("terminalFilter");
        auto *count = window.findChild<QLabel *>("filterStatus");
        mode->setCurrentIndex(1);
        query->setText("\"error*\" && temp >= 20");
        QCOMPARE(terminal->toPlainText(), QString("error flash temp=23"));
        QVERIFY(count->text().startsWith("1 / 4"));
        auto *highlight = window.findChild<QCheckBox *>("filterHighlight");
        highlight->setChecked(true);
        QVERIFY(terminal->toPlainText().contains("info temp=1"));
        QCOMPARE(terminal->extraSelections().size(), 1);
        const auto output = directory.filePath("filtered.txt");
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QFileDialog *>();
            QVERIFY(dialog); dialog->selectFile(output);
            QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
        });
        window.findChild<QPushButton *>("saveFiltered")->click();
        QFile saved(output); QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), QByteArray("error flash temp=23"));
        window.findChild<QPushButton *>("saveProfile")->click();
        IniFile restored; QVERIFY(restored.load(config.ini.path, error));
        QCOMPARE(restored.profiles.first().filterMode, QString("expression"));
        QCOMPARE(restored.profiles.first().filterText, query->text());
        QVERIFY(restored.profiles.first().filterHighlight);
        query->setText("\"error\" &&");
        QVERIFY(count->text().startsWith("Position "));
        QVERIFY(!window.findChild<QPushButton *>("saveFiltered")->isEnabled());
        QVERIFY(terminal->toPlainText().contains("info temp=1"));
        QVERIFY(terminal->extraSelections().isEmpty());
        query->setText("\"nomatch\"");
        QVERIFY(count->text().startsWith("0 / 4"));
        highlight->setChecked(false);
        QCOMPARE(terminal->toPlainText(), QString());
        query->clear();
        QVERIFY(terminal->toPlainText().contains("error timeout temp=5"));
        mode->setCurrentIndex(0);
        query->setText("*");
        QCOMPARE(terminal->toPlainText(), QString());
    }
};
QTEST_MAIN(WindowTests)
#include "test_window.moc"
