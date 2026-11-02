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
class QLineEdit;

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
private slots:
    void applyState(SessionState state);
private:
    enum class ExportMode { Buffer, Selection, Filtered };
    AppConfig *config;
    Profile settings;
    std::unique_ptr<Terminal> terminal;
    std::shared_ptr<DisplayBuffer> display;
    QThread thread;
    SessionWorker *worker;
    SessionState state = SessionState::Disconnected;
    QComboBox *profiles, *port, *baud, *sendText, *newline;
    QCheckBox *readOnly, *echo, *follow, *hexSend, *autoReconnect;
    QLabel *status, *identity, *logStatus;
    QPushButton *sendButton;
    QTextEdit *view;
    QPlainTextEdit *events;
    QTimer *renderTimer;
    QLineEdit *terminalFilter;
    QCheckBox *filterCase;
    QStringList availablePorts;
    QString logPath, logFault;
    QList<QWidget *> connectionEditors;
    void loadProfile(const Profile &profile);
    bool collect();
    void refreshProfiles();
    void refreshPorts();
    void advanced(int tab = 0);
    void updateLogStatus();
    void connectSession();
    void sendMessage();
    void render();
    void renderView();
    void exportTerminal(ExportMode mode);
    void addEvent(const QString &message);
};
