#pragma once
#include "config.h"
#include "session.h"
#include "terminal.h"
#include "filter.h"
#include <QHash>
#include <QKeySequence>
#include <QWidget>
#include <memory>

class QAction;
class QComboBox;
class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTextEdit;
class QLineEdit;
class EsptoolDialog;

class AppConfig : public QObject {
    Q_OBJECT
public:
    IniFile ini;
    explicit AppConfig(QObject *parent = nullptr) : QObject(parent) {}
    bool saveProfile(const Profile &profile, QWidget *parent);
    bool chooseFile(QWidget *parent);
    bool editGlobalSettings(QWidget *parent);
signals:
    void profilesChanged();
    void globalsChanged();
};

void showFilterHelp(QWidget *parent);

// One profile/serial session. Hosted as an MDI sub-window by MainWindow; its actions feed the menus.
class SessionWindow : public QWidget {
    Q_OBJECT
public:
    explicit SessionWindow(AppConfig *config, const Profile &profile, SerialTransport *transport = nullptr);
    ~SessionWindow() override;
    QString profileName() const { return settings.name; }
    QAction *action(const QString &name) const;
    void setShortcutsActive(bool active);
signals:
    void summaryChanged();
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
    EsptoolDialog *diagnosticsDialog = nullptr;
    SessionState state = SessionState::Disconnected;
    QComboBox *port, *baud, *sendText, *newline;
    QAction *readOnly, *follow, *autoReconnect, *splitAction, *connectAction;
    QCheckBox *echo, *hexSend;
    QLabel *status, *identity, *logStatus;
    QPushButton *sendButton;
    QTextEdit *view;
    QPlainTextEdit *events;
    QWidget *filterBar, *sendBar;
    QTimer *renderTimer;
    QLineEdit *terminalFilter;
    QCheckBox *filterCase, *filterHighlight;
    QComboBox *filterMode;
    QLabel *filterStatus;
    QAction *saveFiltered, *saveSelection;
    LineFilter expressionFilter;
    bool filterValid = true;
    bool shuttingDown = false;
    QStringList availablePorts;
    QString logPath, logFault;
    QList<QObject *> connectionEditors;
    QHash<QAction *, QKeySequence> shortcuts;
    QAction *makeAction(const QString &name, const QString &text, const QKeySequence &key = {});
    void loadProfile(const Profile &profile);
    Profile runtimeProfile() const;
    bool collect();
    void refreshPorts();
    void advanced(int tab = 0);
    void updateLogStatus();
    void updateTitle();
    void connectSession();
    void showEsptool();
    void sendMessage();
    void render();
    void renderView();
    void updateFilter();
    bool matchesFilter(const QString &line) const;
    QString filteredText(const QString &text) const;
    void exportTerminal(ExportMode mode);
    void addEvent(const QString &message);
};
