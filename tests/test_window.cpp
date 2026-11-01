#include "window.h"
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QTest>

class WindowTests : public QObject {
    Q_OBJECT
private slots:
    void connectionSettingsFreezeUntilManualPause() {
        AppConfig config; config.ini.path = "unused-smoke-config.ini";
        config.ini.profiles = {Profile{}};
        SessionWindow window(&config, config.ini.profiles.first());
        const QStringList editors = {"profiles", "newProfile", "port", "baud",
                                      "advanced", "forgetBinding", "connectButton"};
        for (const auto state : {SessionState::Waiting, SessionState::Connecting,
                                 SessionState::Connected, SessionState::Loaned}) {
            QVERIFY(QMetaObject::invokeMethod(&window, "applyState", Qt::DirectConnection, Q_ARG(SessionState, state)));
            for (const QString &name : editors) {
                const auto *editor = window.findChild<QWidget *>(name);
                QVERIFY2(editor, qPrintable(name)); QVERIFY2(!editor->isEnabled(), qPrintable(name));
            }
            QVERIFY(!window.findChild<QPushButton *>("sendButton")->isEnabled());
            QVERIFY(window.findChild<QCheckBox *>("readOnly")->isEnabled());
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
};
QTEST_MAIN(WindowTests)
#include "test_window.moc"
