#pragma once
#include "config.h"
#include "session.h"
#include "terminal.h"
#include <QMainWindow>
#include <memory>

class QComboBox;
class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTextEdit;

class AppConfig : public QObject {
    Q_OBJECT
public:
    IniFile ini;
    explicit AppConfig(QObject *parent = nullptr) : QObject(parent) {}
    bool saveProfile(const Profile &profile, QWidget *parent);
    bool chooseFile(QWidget *parent);
signals:
    void profilesChanged();
};

class SessionWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit SessionWindow(AppConfig *config, const Profile &profile);
    ~SessionWindow() override;
private:
    AppConfig *config;
    Profile settings;
    std::unique_ptr<Terminal> terminal;
    std::shared_ptr<DisplayBuffer> display;
    QThread thread;
    SessionWorker *worker;
    SessionState state = SessionState::Disconnected;
    QComboBox *profiles, *port, *baud, *sendText, *newline;
    QCheckBox *readOnly, *echo, *follow, *hexSend;
    QLabel *status, *identity, *logStatus;
    QPushButton *sendButton;
    QTextEdit *view;
    QPlainTextEdit *events;
    QTimer *renderTimer;
    QStringList availablePorts;
    void loadProfile(const Profile &profile);
    bool collect();
    void refreshProfiles();
    void refreshPorts();
    void advanced();
    void connectSession();
    void sendMessage();
    void render();
    void addEvent(const QString &message);
};
