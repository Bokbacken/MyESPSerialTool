#include "window.h"
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTextEdit>
#include <QTextCursor>
#include <QFileDialog>
#include <QFile>
#include <QTest>

class WindowTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
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
};
QTEST_MAIN(WindowTests)
#include "test_window.moc"
