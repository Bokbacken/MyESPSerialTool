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
#include <QInputDialog>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>

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
        QCoreApplication::setApplicationVersion("0.24.0");
    }
    void windowTitleIdentifiesRunningVersion() {
        AppConfig config; config.ini.path = "unused-smoke-config.ini";
        config.ini.profiles = {Profile{}};
        SessionWindow window(&config, config.ini.profiles.first());
        QVERIFY(window.windowTitle().contains("0.24.0"));
        QVERIFY(window.windowTitle().endsWith("ESP"));
        const auto *version = window.findChild<QLabel *>("applicationVersion");
        QVERIFY(version);
        QCOMPARE(version->text(), QString::fromUtf8("Serial ESP Helper — Version 0.24.0"));
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
                QVERIFY(confirmation->text().contains("reset-free"));
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
        plan->findChild<QLineEdit *>("planCapacity")->setText("0x400000");
        QVERIFY(report->toPlainText().contains("Writes remain blocked"));
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
        QVERIFY2(report->toPlainText().contains("WRITE BLOCKED"), qPrintable(report->toPlainText()));
        QVERIFY(recheck->isEnabled());
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
