#include "window.h"
#include "esptool_dialog.h"
#include "flashplan.h"
#include <QCoreApplication>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QScrollArea>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QSplitter>
#include <QGroupBox>
#include <QAction>
#include <QTextEdit>
#include <QTextCursor>
#include <QTextBlock>
#include <QVBoxLayout>
#include <QUrl>

bool AppConfig::chooseFile(QWidget *parent) {
    const QString file = QFileDialog::getSaveFileName(parent, "Choose configuration file (no automatic relocation)",
                                                     ini.path, "INI files (*.ini);;All files (*)");
    if (file.isEmpty()) return false;
    QString error;
    IniFile selected;
    if (!selected.load(file, error)) {
        QMessageBox::critical(parent, "Invalid configuration", error); return false;
    }
    // Save As retains the current profiles and original comments/unknown keys.
    const QString previous = ini.path;
    ini.path = file;
    if (!ini.save(error)) {
        ini.path = previous;
        QMessageBox::critical(parent, "Configuration not saved", error); return false;
    }
    emit profilesChanged(); return true;
}
bool AppConfig::saveProfile(const Profile &profile, QWidget *parent) {
    const auto oldProfiles = ini.profiles;
    bool found = false;
    for (auto &p : ini.profiles) if (p.name == profile.name) { p = profile; found = true; break; }
    if (!found) ini.profiles << profile;
    QString error;
    if (!ini.save(error)) {
        const auto result = QMessageBox::warning(parent, "INI is not writable",
            error + "\nChoose another configuration file explicitly? Nothing was silently moved.",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (result != QMessageBox::Yes || !chooseFile(parent)) {
            ini.profiles = oldProfiles; return false;
        }
    }
    emit profilesChanged(); return true;
}

void showFilterHelp(QWidget *parent) {
    QMessageBox help(parent);
    help.setWindowTitle("Line filters");
    help.setTextFormat(Qt::PlainText);
    help.setTextInteractionFlags(Qt::TextSelectableByMouse);
    help.setText("Text mode: literal substring search.\n"
        "Expression mode: quoted search terms, ! (NOT), && (AND), || (OR), parentheses.\n"
        "Precedence: ! then && then ||.\n"
        "Example: (\"error*flash\" || \"warn??\") && !\"ignored\"\n"
        "? = one Unicode character; * = zero or more characters, on the same line.\n"
        "Escapes inside quotes: \\? \\* \\\" \\\\ (literal ?, *, quote, backslash).\n\n"
        "Numeric fields: temp >= 20 && temp < 30 matches temp=23.5 or temp: 23.5.\n"
        "Position: number(2) >= -1.5e2 selects the second standalone number (1-based).\n"
        "Comparisons: < <= == != >= >. Decimal point, optional sign/exponent.\n"
        "Missing/invalid numbers never satisfy comparisons (including !=).\n"
        "For a repeated field name, the first valid numeric assignment is used.\n"
        "Numbers must be standalone; 32 in ESP32 and 23 in 23C are not numbers.\n"
        "Combine text/numeric tests: \"sensor*\" && voltage >= 3.1.\n\n"
        "Match case applies to text and field names. Empty filter matches all.\n"
        "File > Save profile stores this filter. Invalid syntax disables filtered export.\n"
        "Highlight keeps all rows visible; filtered export still saves only matches.\n"
        "Filters/export use the retained terminal buffer, never change the raw log.\n"
        "Saved filters follow INI rules: no leading/trailing whitespace or whitespace before ;/#.\n"
        "Expressions: up to 4096 characters, 128 terms/operators, 32 nesting levels.");
    help.exec();
}

bool AppConfig::editGlobalSettings(QWidget *parent) {
    QDialog dialog(parent);
    dialog.setObjectName("globalSettings");
    dialog.setWindowTitle("Global settings");
    dialog.resize(680, 420);
    auto *layout = new QVBoxLayout(&dialog);
    auto *scope = new QLabel("Shared by all profiles and sessions. A profile can override the log folder "
                             "and esptool values under Settings > Profile.\nINI file: " + ini.path);
    scope->setObjectName("globalScope");
    scope->setWordWrap(true);
    scope->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(scope);
    auto *logs = new QGroupBox("Logging");
    auto *logForm = new QFormLayout(logs);
    auto *logDirectory = new QLineEdit(ini.defaultLogDirectory);
    logDirectory->setObjectName("defaultLogDirectory");
    auto *browseLogs = new QPushButton("Browse…");
    auto *logRow = new QHBoxLayout; logRow->addWidget(logDirectory, 1); logRow->addWidget(browseLogs);
    logForm->addRow("Default log folder", logRow);
    layout->addWidget(logs);
    auto *esp = new QGroupBox("ESP tools (esptool)");
    auto *espForm = new QFormLayout(esp);
    auto *path = new QLineEdit(ini.esptoolPath);
    path->setObjectName("globalEsptoolPath");
    path->setPlaceholderText("Empty = esptool found on PATH");
    auto *browseTool = new QPushButton("Browse…");
    auto *toolRow = new QHBoxLayout; toolRow->addWidget(path, 1); toolRow->addWidget(browseTool);
    espForm->addRow("esptool executable", toolRow);
    auto *chip = new QComboBox;
    chip->setObjectName("globalEsptoolChip");
    chip->addItem("Any chip (no expectation)", QString());
    for (const auto &name : FlashPlan::supportedChips()) chip->addItem(name, name);
    chip->setCurrentIndex(qMax(0, chip->findData(ini.esptoolDefaultChip)));
    espForm->addRow("Expected chip", chip);
    auto *baud = new QComboBox;
    baud->setObjectName("globalEsptoolBaud");
    baud->setEditable(true);
    for (int rate : {115200, 230400, 460800, 921600, 1500000, 2000000}) baud->addItem(QString::number(rate));
    baud->setCurrentText(QString::number(ini.esptoolBaud));
    espForm->addRow("esptool baud", baud);
    layout->addWidget(esp);
    auto *warnings = new QGroupBox("Warnings");
    auto *warningForm = new QVBoxLayout(warnings);
    auto *confirmReset = new QCheckBox("Ask before diagnostics that may reset the device");
    confirmReset->setObjectName("confirmReset");
    confirmReset->setChecked(ini.confirmReset);
    warningForm->addWidget(confirmReset);
    auto *fixed = new QLabel("Wrong-chip blocks and the final flash confirmation cannot be disabled.");
    fixed->setWordWrap(true);
    warningForm->addWidget(fixed);
    layout->addWidget(warnings);
    layout->addStretch();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(browseLogs, &QPushButton::clicked, &dialog, [&] {
        const QString selected = QFileDialog::getExistingDirectory(&dialog, "Default log folder",
            QDir(QFileInfo(ini.path).absolutePath()).absoluteFilePath(logDirectory->text()));
        if (!selected.isEmpty()) logDirectory->setText(selected);
    });
    connect(browseTool, &QPushButton::clicked, &dialog, [&] {
        const QString selected = QFileDialog::getOpenFileName(&dialog, "esptool executable", path->text());
        if (!selected.isEmpty()) path->setText(selected);
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const auto unsafe = [](const QString &value) {
            return value != value.trimmed() || value.size() > 4096
                || value.contains(QRegularExpression("[\\r\\n\\x00]"))
                || value.contains(QRegularExpression("\\s[;#]"));
        };
        quint64 baudValue = 0;
        if (unsafe(path->text()) || logDirectory->text().trimmed().isEmpty() || unsafe(logDirectory->text())
            || !FlashPlan::parseNumber(baud->currentText(), baudValue)
            || baudValue < 1200 || baudValue > 4000000) {
            QMessageBox::warning(&dialog, "Invalid global settings",
                "Check the esptool path, baud (1200–4000000) and log folder. Paths cannot have surrounding "
                "spaces, line breaks or whitespace before ; or #.");
            return;
        }
        const IniFile previous = ini;
        ini.defaultLogDirectory = logDirectory->text();
        ini.esptoolPath = path->text();
        ini.esptoolDefaultChip = chip->currentData().toString();
        ini.esptoolBaud = int(baudValue);
        ini.confirmReset = confirmReset->isChecked();
        QString error;
        if (!ini.save(error)) {
            ini = previous;
            QMessageBox::critical(&dialog, "Global settings not saved", error);
            return;
        }
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted) return false;
    emit globalsChanged();
    return true;
}

static QString stateStyle(SessionState s) {
    QString color = "#6c757d";
    if (s == SessionState::Connected) color = "#1e7e34";
    else if (s == SessionState::Waiting || s == SessionState::Connecting) color = "#a66a00";
    else if (s == SessionState::Loaned) color = "#6f42c1";
    return QString("QLabel { background:%1; color:white; border-radius:3px; padding:2px 8px; font-weight:bold; }")
        .arg(color);
}

QAction *SessionWindow::makeAction(const QString &name, const QString &text, const QKeySequence &key) {
    auto *created = new QAction(text, this);
    created->setObjectName(name);
    if (!key.isEmpty()) shortcuts.insert(created, key);
    return created;
}
QAction *SessionWindow::action(const QString &name) const {
    return findChild<QAction *>(name, Qt::FindDirectChildrenOnly);
}
void SessionWindow::setShortcutsActive(bool active) {
    // Only the active MDI session owns shortcuts, so F5 etc. never act on a background session.
    for (auto it = shortcuts.cbegin(); it != shortcuts.cend(); ++it)
        it.key()->setShortcut(active ? it.value() : QKeySequence());
}

SessionWindow::SessionWindow(AppConfig *c, const Profile &p, SerialTransport *transport)
    : config(c), settings(p), display(std::make_shared<DisplayBuffer>()),
      worker(new SessionWorker(display, transport)) {
    resize(1000, 700);
    auto *saveProfile = makeAction("saveProfile", "&Save profile", QKeySequence::Save);
    saveProfile->setStatusTip("Write this session's profile settings to the INI file");
    auto *saveBuffer = makeAction("saveBuffer", "Whole terminal &buffer…");
    saveSelection = makeAction("saveSelection", "&Selected text…");
    saveSelection->setEnabled(false);
    saveFiltered = makeAction("saveFiltered", "&Filtered rows…");
    saveFiltered->setEnabled(false);
    auto *openLogs = makeAction("openLogFolder", "Open &log folder");
    auto *copy = makeAction("copyText", "&Copy", QKeySequence::Copy);
    copy->setEnabled(false);
    auto *selectAll = makeAction("selectAll", "Select &all", QKeySequence::SelectAll);
    auto *find = makeAction("findFilter", "&Filter lines…", QKeySequence::Find);
    auto *clear = makeAction("clearView", "C&lear terminal view");
    clear->setStatusTip("Clears only the rendered view; reception and log files continue unchanged");
    follow = makeAction("follow", "&Follow new output");
    follow->setCheckable(true);
    auto *showFilter = makeAction("showFilterBar", "Filter &bar");
    auto *showSend = makeAction("showSendBar", "&Send bar");
    auto *showEvents = makeAction("showEvents", "&Event log");
    for (auto *toggle : {showFilter, showSend, showEvents}) { toggle->setCheckable(true); toggle->setChecked(true); }
    connectAction = makeAction("connectButton", "&Connect / Resume", Qt::Key_F5);
    connectAction->setIconText("Connect");
    auto *pause = makeAction("pauseButton", "&Pause (release port)", Qt::Key_F6);
    pause->setIconText("Pause");
    pause->setStatusTip("Disconnects, releases the port and blocks reconnect until Connect / Resume");
    splitAction = makeAction("splitLog", "Spli&t log (start new log set)", Qt::Key_F7);
    splitAction->setIconText("Split log");
    splitAction->setToolTip("Close the current log files and start a new log set without disconnecting");
    splitAction->setEnabled(false);
    readOnly = makeAction("readOnly", "&Read-only (block sending)");
    readOnly->setIconText("Read-only");
    readOnly->setCheckable(true);
    autoReconnect = makeAction("autoReconnect", "Reconnect &automatically after connection loss");
    autoReconnect->setCheckable(true);
    auto *forget = makeAction("forgetBinding", "&Forget USB binding");
    auto *diagnostics = makeAction("esptoolDiagnostics", "ESP &diagnostics…", Qt::Key_F9);
    diagnostics->setIconText("Diagnostics");
    auto *flash = makeAction("flashFirmware", "&Flash firmware…");
    flash->setIconText("Flash…");
    auto *connectionSettings = makeAction("advanced", "&Connection…");
    auto *loggingSettings = makeAction("logSettings", "&Logging…");
    auto *terminalSettings = makeAction("terminalSettings", "&Terminal…");
    auto *espSettings = makeAction("espSettings", "&ESP tools…");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    auto *bar = new QToolBar;
    bar->setObjectName("sessionToolbar");
    bar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    port = new QComboBox; port->setEditable(true); port->setObjectName("port");
    port->setMinimumContentsLength(12);
    port->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    baud = new QComboBox; baud->setEditable(true); baud->setObjectName("baud");
    for (int rate : {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600})
        baud->addItem(QString::number(rate));
    bar->addWidget(new QLabel("Port "));
    bar->addWidget(port);
    bar->addWidget(new QLabel("  Baud "));
    bar->addWidget(baud);
    bar->addSeparator();
    bar->addAction(connectAction);
    bar->addAction(pause);
    bar->addAction(splitAction);
    bar->addSeparator();
    bar->addAction(readOnly);
    auto *spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(spacer);
    bar->addAction(diagnostics);
    bar->addAction(flash);
    layout->addWidget(bar);

    auto *infoRow = new QHBoxLayout;
    status = new QLabel("Disconnected"); status->setObjectName("sessionState");
    status->setStyleSheet(stateStyle(SessionState::Disconnected));
    identity = new QLabel; identity->setTextInteractionFlags(Qt::TextSelectableByMouse);
    identity->setObjectName("usbIdentity");
    auto *followButton = new QToolButton;
    followButton->setDefaultAction(follow);
    followButton->setText("Follow output");
    infoRow->addWidget(status);
    infoRow->addWidget(identity, 1);
    infoRow->addWidget(followButton);
    layout->addLayout(infoRow);
    logStatus = new QLabel("Raw logging disabled"); logStatus->setObjectName("logStatus");
    logStatus->setWordWrap(true);
    logStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(logStatus);

    view = new QTextEdit; view->setReadOnly(true); view->setObjectName("terminal");
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    view->setLineWrapMode(QTextEdit::NoWrap);
    view->setStyleSheet("QTextEdit { background:#151515; color:#dddddd; }");
    events = new QPlainTextEdit; events->setReadOnly(true);
    events->setMaximumBlockCount(200);
    events->setObjectName("events");
    events->setPlaceholderText("Session events");
    auto *splitter = new QSplitter(Qt::Vertical);
    splitter->setObjectName("terminalSplitter");
    splitter->addWidget(view);
    splitter->addWidget(events);
    splitter->setStretchFactor(0, 5);
    splitter->setStretchFactor(1, 1);
    splitter->setCollapsible(0, false);
    splitter->setSizes({560, 110});
    layout->addWidget(splitter, 1);

    filterBar = new QWidget;
    filterBar->setObjectName("filterBar");
    auto *filterLayout = new QVBoxLayout(filterBar);
    filterLayout->setContentsMargins(0, 0, 0, 0);
    auto *filterRow = new QHBoxLayout;
    terminalFilter = new QLineEdit;
    terminalFilter->setObjectName("terminalFilter"); terminalFilter->setMaxLength(4096);
    terminalFilter->setClearButtonEnabled(true);
    filterCase = new QCheckBox("Match case");
    filterCase->setObjectName("filterCase");
    filterMode = new QComboBox;
    filterMode->setObjectName("filterMode"); filterMode->addItems({"Text", "Expression"});
    filterHighlight = new QCheckBox("Highlight only");
    filterHighlight->setToolTip("Keep all rows visible and highlight matches");
    filterHighlight->setObjectName("filterHighlight");
    auto *filterHelp = new QToolButton;
    filterHelp->setText("?");
    filterHelp->setToolTip("Filter syntax");
    filterRow->addWidget(new QLabel("Filter"));
    filterRow->addWidget(filterMode); filterRow->addWidget(terminalFilter, 1);
    filterRow->addWidget(filterCase); filterRow->addWidget(filterHighlight); filterRow->addWidget(filterHelp);
    filterLayout->addLayout(filterRow);
    filterStatus = new QLabel;
    filterStatus->setObjectName("filterStatus"); filterStatus->setWordWrap(true);
    filterLayout->addWidget(filterStatus);
    layout->addWidget(filterBar);

    sendBar = new QWidget;
    sendBar->setObjectName("sendBar");
    auto *sendRow = new QHBoxLayout(sendBar);
    sendRow->setContentsMargins(0, 0, 0, 0);
    sendText = new QComboBox; sendText->setEditable(true); sendText->setObjectName("sendText");
    sendText->setInsertPolicy(QComboBox::NoInsert);
    sendText->lineEdit()->setMaxLength(32768);
    sendText->lineEdit()->setPlaceholderText("Text to send (Enter sends)");
    newline = new QComboBox; newline->addItems({"none", "lf", "cr", "crlf"});
    newline->setToolTip("Line ending appended to each message");
    echo = new QCheckBox("Local echo"); hexSend = new QCheckBox("Hex bytes");
    hexSend->setToolTip("Pairs of hexadecimal digits, optional spaces. Newline is appended separately.");
    sendButton = new QPushButton("Send"); sendButton->setObjectName("sendButton");
    sendRow->addWidget(new QLabel("Send"));
    sendRow->addWidget(sendText, 1); sendRow->addWidget(new QLabel("Line end"));
    sendRow->addWidget(newline); sendRow->addWidget(echo);
    sendRow->addWidget(hexSend); sendRow->addWidget(sendButton);
    layout->addWidget(sendBar);
    connectionEditors = {port, baud, forget, connectAction};

    connect(loggingSettings, &QAction::triggered, this, [this] { advanced(1); });
    connect(connectionSettings, &QAction::triggered, this, [this] { advanced(0); });
    connect(terminalSettings, &QAction::triggered, this, [this] { advanced(2); });
    connect(espSettings, &QAction::triggered, this, [this] { advanced(3); });
    connect(openLogs, &QAction::triggered, this, [this] {
        const auto directory = QDir(QFileInfo(config->ini.path).absolutePath())
            .absoluteFilePath(runtimeProfile().logDirectory);
        if (!QDir().mkpath(directory) || !QDesktopServices::openUrl(QUrl::fromLocalFile(directory)))
            addEvent("Cannot open log folder: " + directory);
    });
    connect(copy, &QAction::triggered, view, &QTextEdit::copy);
    connect(selectAll, &QAction::triggered, view, &QTextEdit::selectAll);
    connect(find, &QAction::triggered, this, [this, showFilter] {
        showFilter->setChecked(true);
        terminalFilter->setFocus();
        terminalFilter->selectAll();
    });
    connect(showFilter, &QAction::toggled, filterBar, &QWidget::setVisible);
    connect(showSend, &QAction::toggled, sendBar, &QWidget::setVisible);
    connect(showEvents, &QAction::toggled, events, &QWidget::setVisible);
    connect(terminalFilter, &QLineEdit::textChanged, this, [this] { updateFilter(); });
    connect(filterCase, &QCheckBox::toggled, this, [this] { updateFilter(); });
    connect(filterMode, &QComboBox::currentIndexChanged, this, [this] { updateFilter(); });
    connect(filterHighlight, &QCheckBox::toggled, this, [this] { updateFilter(); });
    connect(filterHelp, &QToolButton::clicked, this, [this] { showFilterHelp(this); });
    connect(view, &QTextEdit::selectionChanged, this, [this, copy] {
        const bool selected = view->textCursor().hasSelection();
        saveSelection->setEnabled(selected);
        copy->setEnabled(selected);
    });
    connect(saveBuffer, &QAction::triggered, this, [this] { exportTerminal(ExportMode::Buffer); });
    connect(saveSelection, &QAction::triggered, this, [this] { exportTerminal(ExportMode::Selection); });
    connect(saveFiltered, &QAction::triggered, this, [this] { exportTerminal(ExportMode::Filtered); });
    loadProfile(p);
    worker->moveToThread(&thread);
    connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &SessionWorker::stateChanged, this, &SessionWindow::applyState);
    connect(worker, &SessionWorker::toolCancelRequested, this, [this] {
        if (diagnosticsDialog) diagnosticsDialog->cancelCurrent();
    });
    connect(worker, &SessionWorker::problem, this, &SessionWindow::addEvent);
    connect(worker, &SessionWorker::logFileChanged, this, [this](const QString &path) {
        logPath = path; updateLogStatus();
    });
    connect(worker, &SessionWorker::logSplit, this, [this](const QString &previous) {
        logFault.clear();
        addEvent("Log split: closed " + QFileInfo(previous).fileName()
                 + " and started a new log set; the port stayed open.");
    });
    connect(worker, &SessionWorker::portsAvailable, this, [this](const QStringList &ports) {
        availablePorts = ports; refreshPorts();
    });
    connect(worker, &SessionWorker::selectedPortChanged, this, [this](const QString &selected) {
        settings.port = selected;
        port->setCurrentText(selected);
        updateTitle();
    });
    connect(worker, &SessionWorker::identityBound, this, [this](const Profile &bound) {
        settings.usbSerial = bound.usbSerial; settings.vid = bound.vid; settings.pid = bound.pid;
        identity->setText(QString("Bound USB: %1 VID=%2 PID=%3 (File > Save profile to persist)")
            .arg(settings.usbSerial).arg(settings.vid).arg(settings.pid));
    });
    connect(worker, &SessionWorker::sent, this, [this](const QByteArray &bytes) {
        if (echo->isChecked()) {
            render();
            const int scroll = view->verticalScrollBar()->value();
            terminal->localEcho(bytes); renderView();
            view->verticalScrollBar()->setValue(follow->isChecked() ? view->verticalScrollBar()->maximum() : scroll);
        }
    });
    connect(config, &AppConfig::globalsChanged, this, [this] { updateLogStatus(); });
    connect(saveProfile, &QAction::triggered, this, [this] {
        if (collect() && config->saveProfile(settings, this)) addEvent("Profile saved atomically; previous INI in .bak");
    });
    connect(autoReconnect, &QAction::toggled, this, [this](bool enabled) {
        settings.autoReconnect = enabled;
        QMetaObject::invokeMethod(worker, [worker = worker, enabled] { worker->setAutoReconnect(enabled); },
                                  Qt::QueuedConnection);
        addEvent(enabled ? "Automatic reconnect enabled. Save profile to persist."
                         : "Automatic reconnect disabled. Save profile to persist.");
    });
    connect(connectAction, &QAction::triggered, this, &SessionWindow::connectSession);
    connect(diagnostics, &QAction::triggered, this, &SessionWindow::showEsptool);
    connect(flash, &QAction::triggered, this, [this] {
        showEsptool();
        if (diagnosticsDialog) diagnosticsDialog->openFlashPlan();
    });
    connect(pause, &QAction::triggered, this, [this] {
        QMetaObject::invokeMethod(worker, &SessionWorker::pause, Qt::QueuedConnection);
    });
    connect(splitAction, &QAction::triggered, this, [this] {
        QMetaObject::invokeMethod(worker, &SessionWorker::splitLog, Qt::QueuedConnection);
    });
    connect(forget, &QAction::triggered, this, [this] {
        QMetaObject::invokeMethod(worker, &SessionWorker::pause, Qt::QueuedConnection);
        settings.usbSerial.clear(); settings.vid = settings.pid = -1;
        identity->setText("No stable USB binding; only the explicit selected port is used.");
        addEvent("USB binding forgotten; session paused. Connect and Save to bind the new device.");
    });
    connect(clear, &QAction::triggered, this, [this] { terminal->clear(); renderView(); });
    connect(sendButton, &QPushButton::clicked, this, &SessionWindow::sendMessage);
    connect(sendText->lineEdit(), &QLineEdit::returnPressed, this, &SessionWindow::sendMessage);
    connect(readOnly, &QAction::toggled, this, [this](bool checked) {
        settings.readOnly = checked;
        sendButton->setEnabled(state == SessionState::Connected && !checked);
        QMetaObject::invokeMethod(worker, [worker = worker, checked] { worker->setReadOnly(checked); },
                                  Qt::QueuedConnection);
        addEvent("Read-only send gate changed without opening/closing the port or touching DTR/RTS.");
    });
    renderTimer = new QTimer(this); renderTimer->setInterval(50);
    connect(renderTimer, &QTimer::timeout, this, &SessionWindow::render); renderTimer->start();
    connect(&thread, &QThread::started, worker, &SessionWorker::monitor);
    thread.start();
    addEvent("Event-driven serial. Pause releases the port and suppresses reconnect.");
    addEvent("DTR/RTS are explicit connection settings, independent of read-only send gating.");
    addEvent("OS/adapter opening or closing may pulse physical lines. Read-only toggles never reopen the port.");
    if (p.autoConnect) QTimer::singleShot(0, this, &SessionWindow::connectSession);
}
void SessionWindow::applyState(SessionState s) {
    state = s; status->setText(stateName(s));
    status->setStyleSheet(stateStyle(s));
    updateLogStatus();
    const bool active = diagnosticsDialog || s == SessionState::Waiting
        || s == SessionState::Connecting || s == SessionState::Connected
        || s == SessionState::Loaned;
    for (QObject *editor : connectionEditors) {
        editor->setProperty("enabled", !active);
        editor->setProperty("toolTip", !active ? "" : diagnosticsDialog
            ? "Close the esptool diagnostics window before changing ports."
            : "Pause before editing connection settings");
    }
    splitAction->setEnabled(s == SessionState::Connected);
    sendButton->setEnabled(s == SessionState::Connected && !settings.readOnly);
    if (s != SessionState::Connected && s != SessionState::Connecting) {
        render(); terminal->finish(); renderView();
    }
    updateTitle();
}
void SessionWindow::updateTitle() {
    const QString target = settings.port.isEmpty() ? QString("no port") : settings.port;
    setWindowTitle(QString("%1 (%2) — %3").arg(settings.name, target, stateName(state)));
    emit summaryChanged();
}
SessionWindow::~SessionWindow() {
    shuttingDown = true;
    renderTimer->stop();
    if (diagnosticsDialog) diagnosticsDialog->cancelAndWait();
    QMetaObject::invokeMethod(worker, &SessionWorker::shutdown, Qt::BlockingQueuedConnection);
    thread.quit(); thread.wait();
}
void SessionWindow::loadProfile(const Profile &p) {
    settings = p;
    port->setCurrentText(p.port); baud->setCurrentText(QString::number(p.baud));
    readOnly->blockSignals(true); readOnly->setChecked(p.readOnly); readOnly->blockSignals(false);
    autoReconnect->blockSignals(true); autoReconnect->setChecked(p.autoReconnect); autoReconnect->blockSignals(false);
    logPath.clear(); logFault.clear(); updateLogStatus();
    echo->setChecked(p.localEcho); follow->setChecked(p.follow); newline->setCurrentText(p.newline);
    terminal = std::make_unique<Terminal>(p.terminalRows, p.lineColumns);
    quint64 dropped; display->take(dropped); view->clear(); sendText->clear(); sendButton->setEnabled(false);
    for (auto *editor : QList<QObject *>{terminalFilter, filterMode, filterCase, filterHighlight})
        editor->blockSignals(true);
    terminalFilter->setText(p.filterText);
    filterMode->setCurrentIndex(p.filterMode == "expression" ? 1 : 0);
    filterCase->setChecked(p.filterCase); filterHighlight->setChecked(p.filterHighlight);
    for (auto *editor : QList<QObject *>{terminalFilter, filterMode, filterCase, filterHighlight})
        editor->blockSignals(false);
    updateFilter();
    identity->setText(p.usbSerial.isEmpty() ? "No stable USB binding; only the explicit selected port is used."
        : QString("Bound USB: %1 VID=%2 PID=%3 (port field cannot override this binding)")
            .arg(p.usbSerial).arg(p.vid).arg(p.pid));
    updateTitle();
}
Profile SessionWindow::runtimeProfile() const {
    Profile resolved = settings;
    if (resolved.logDirectory.isEmpty()) resolved.logDirectory = config->ini.defaultLogDirectory;
    if (resolved.esptoolPath.isEmpty()) resolved.esptoolPath = config->ini.esptoolPath;
    if (resolved.esptoolChip.isEmpty()) resolved.esptoolChip = config->ini.esptoolDefaultChip;
    if (resolved.esptoolBaud == 0) resolved.esptoolBaud = config->ini.esptoolBaud;
    return resolved;
}
bool SessionWindow::collect() {
    auto values = settings.values();
    values["port"] = port->currentText(); values["baud"] = baud->currentText();
    values["readOnly"] = readOnly->isChecked() ? "true" : "false";
    values["localEcho"] = echo->isChecked() ? "true" : "false";
    values["follow"] = follow->isChecked() ? "true" : "false";
    values["newline"] = newline->currentText();
    values["filterText"] = terminalFilter->text();
    values["filterMode"] = filterMode->currentIndex() == 1 ? "expression" : "text";
    values["filterCase"] = filterCase->isChecked() ? "true" : "false";
    values["filterHighlight"] = filterHighlight->isChecked() ? "true" : "false";
    Profile checked; QString error;
    if (!Profile::fromValues(settings.name, values, checked, error)) {
        QMessageBox::warning(this, "Invalid profile", error); return false;
    }
    settings = checked; return true;
}
void SessionWindow::refreshPorts() {
    if (port->view()->isVisible() || port->lineEdit()->hasFocus()) return;
    const QString selected = port->currentText();
    port->blockSignals(true); port->clear();
    port->addItems(availablePorts);
    port->setCurrentText(selected); port->blockSignals(false);
}
void SessionWindow::advanced(int tab) {
    if (!collect()) return;
    QDialog dialog(this); dialog.setWindowTitle("Profile settings — " + settings.name);
    dialog.setObjectName("profileSettings");
    dialog.resize(760, 580);
    auto *layout = new QVBoxLayout(&dialog);
    auto *notice = new QLabel(QString(
        "These settings belong to profile '%1'. Empty override fields use the shared defaults "
        "from Settings > Global settings.\n"
        "Save profile writes all tabs to the INI file. Apply uses them in this session only. "
        "Both pause the session; use Connect / Resume afterwards.")
        .arg(settings.name));
    notice->setObjectName("settingsScope");
    notice->setWordWrap(true); layout->addWidget(notice);
    auto *tabs = new QTabWidget;
    QList<QFormLayout *> forms;
    for (const auto &name : {"Connection", "Logging", "Terminal", "ESP tools"}) {
        auto *page = new QWidget;
        forms << new QFormLayout(page);
        forms.last()->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        forms.last()->setRowWrapPolicy(QFormLayout::WrapLongRows);
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true); scroll->setWidget(page);
        tabs->addTab(scroll, name);
    }
    tabs->setCurrentIndex(tab); layout->addWidget(tabs);
    QMap<QString, QWidget *> editors;
    auto values = settings.values();
    const QMap<QString, QStringList> options = {
        {"dataBits", {"5", "6", "7", "8"}}, {"parity", {"none", "even", "odd", "space", "mark"}},
        {"stopBits", {"1", "1.5", "2"}}, {"flowControl", {"none", "hardware", "software"}},
        {"logMode", {"text", "raw", "both"}}};
    const QStringList flags = {"dtr", "rts", "logging", "timestampMetadata", "autoConnect",
                               "autoReconnect", "newLogOnReconnect", "terminalTimestamp",
                               "textTimestamp", "timestampMilliseconds"};
    const QMap<QString, int> groups = {
        {"esptoolPath", 3}, {"esptoolChip", 3}, {"esptoolBaud", 3},
        {"terminalRows", 2}, {"lineColumns", 2}, {"displayQueueBytes", 2}, {"terminalTimestamp", 2},
        {"newLogOnReconnect", 1}, {"timestampMetadata", 1}, {"rawLogExtension", 1},
        {"textTimestamp", 1}, {"timestampMilliseconds", 1}};
    const QList<QPair<QString, QString>> fields = {
        {"dataBits", "Data bits"}, {"parity", "Parity"}, {"stopBits", "Stop bits"},
        {"flowControl", "Flow control"}, {"dtr", "DTR (applied on open, independent of read-only)"},
        {"rts", "RTS (applied on open unless hardware flow manages it)"},
        {"autoConnect", "Connect when the session opens and retry failed initial opens"},
        {"autoReconnect", "Reconnect this session after connection loss"},
        {"writeTimeoutMs", "Write completion timeout (100–60000 ms)"},
        {"retryLimit", "Open retries per outage (1–100, at 1 Hz)"},
        {"logging", "Receive logging enabled (session metadata is always recorded)"},
        {"logMode", "Log format: Text, Raw (exact bytes), or Both"},
        {"logDirectory", "Log folder override"},
        {"logName", "Filename template (without extension)"},
        {"logExtension", "Text log extension (default .log)"},
        {"rawLogExtension", "Raw exact byte-log extension (default .raw)"},
        {"logBytes", "Rotate after received bytes (1–1073741824; text expansion is additional)"},
        {"logSeconds", "Rotate after seconds (0 = size only; max 31536000)"},
        {"logRetention", "Rolling buffer: completed segments kept (1–100), plus active"},
        {"newLogOnReconnect", "New log set on reconnect (off = append to current log)"},
        {"logQueueBytes", "Disk queue byte limit (1–67108864)"},
        {"timestampMetadata", "Separate reception UTC raw-offset metadata (not device time)"},
        {"textTimestamp", "Add host reception UTC timestamps to text log"},
        {"timestampMilliseconds", "Timestamp precision: 3 decimal places in seconds (terminal + text log)"},
        {"terminalRows", "Maximum terminal rows (10–10000)"},
        {"lineColumns", "Wrap at columns (32–16384; total cells capped at 1 Mi)"},
        {"displayQueueBytes", "Display receive queue (1024–67108864 bytes; separate from disk log)"},
        {"terminalTimestamp", "Show host reception UTC timestamps in terminal"},
        {"esptoolPath", "esptool executable override"},
        {"esptoolChip", "Expected chip override"},
        {"esptoolBaud", "esptool baud override (0 = global)"}};
    for (const auto &field : fields) {
        QWidget *editor;
        if (options.contains(field.first)) {
            auto *combo = new QComboBox; combo->addItems(options[field.first]);
            combo->setCurrentText(values[field.first]); editor = combo;
        } else if (flags.contains(field.first)) {
            auto *box = new QCheckBox; box->setChecked(values[field.first] == "true"); editor = box;
        } else editor = new QLineEdit(values[field.first]);
        editor->setObjectName(field.first);
        int group = groups.value(field.first, field.first.startsWith("log") ? 1 : 0);
        editors[field.first] = editor; forms[group]->addRow(field.second, editor);
    }
    qobject_cast<QLineEdit *>(editors["logDirectory"])->setPlaceholderText(
        "Empty = global default: " + config->ini.defaultLogDirectory);
    qobject_cast<QLineEdit *>(editors["esptoolPath"])->setPlaceholderText(
        "Empty = global: " + (config->ini.esptoolPath.isEmpty() ? QString("esptool on PATH") : config->ini.esptoolPath));
    qobject_cast<QLineEdit *>(editors["esptoolChip"])->setPlaceholderText(
        "Empty = global: " + (config->ini.esptoolDefaultChip.isEmpty() ? QString("any chip") : config->ini.esptoolDefaultChip));
    auto *namingHelp = new QLabel("Fields: {date}, {time} (UTC), {profile}, {port}, {serial}, {id}.\n"
        "Example: {profile}-{date}-{time}. ID is included only when {id} is in the template.\n"
        "Existing/in-use names get a numeric suffix for a new session; existing logs are not overwritten.\n"
        "Rotation uses the chosen extension plus .1, .2, etc.; oldest segments of this log set are deleted.\n"
        "Size/time limits: whichever comes first. Time rotation happens on the next received bytes.\n"
        "Session > Split log closes the current log set and starts a new one without disconnecting.\n"
        "Other log sets are never deleted automatically. Traffic logging captures received bytes only.");
    namingHelp->setWordWrap(true); forms[1]->addRow(namingHelp);
    auto *browse = new QPushButton("Choose log folder…");
    forms[1]->insertRow(forms[1]->rowCount() - 1, "", browse);
    connect(browse, &QPushButton::clicked, &dialog, [&] {
        auto *directory = qobject_cast<QLineEdit *>(editors["logDirectory"]);
        const QString selected = QFileDialog::getExistingDirectory(&dialog, "Log folder",
            QDir(QFileInfo(config->ini.path).absolutePath()).absoluteFilePath(
                directory->text().isEmpty() ? config->ini.defaultLogDirectory : directory->text()));
        if (!selected.isEmpty()) directory->setText(selected);
    });
    auto *espNote = new QLabel("Leave these empty to use the shared esptool settings. A profile override "
                               "is useful when one device needs another chip check or baud rate.");
    espNote->setWordWrap(true); forms[3]->insertRow(0, espNote);
    auto *flow = qobject_cast<QComboBox *>(editors["flowControl"]);
    auto *rts = qobject_cast<QCheckBox *>(editors["rts"]);
    auto updateRts = [flow, rts] { rts->setEnabled(flow->currentText() != "hardware"); };
    connect(flow, &QComboBox::currentTextChanged, &dialog, updateRts); updateRts();
    auto *logMode = qobject_cast<QComboBox *>(editors["logMode"]);
    auto updateLogMode = [&] {
        const bool text = logMode->currentText() != "raw";
        const bool raw = logMode->currentText() != "text";
        editors["logExtension"]->setEnabled(text);
        editors["textTimestamp"]->setEnabled(text);
        editors["rawLogExtension"]->setEnabled(raw);
        editors["timestampMetadata"]->setEnabled(raw);
    };
    connect(logMode, &QComboBox::currentTextChanged, &dialog, updateLogMode); updateLogMode();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Apply to session (not saved)");
    auto *save = buttons->addButton("Save profile", QDialogButtonBox::ActionRole);
    save->setObjectName("saveSettings");
    save->setDefault(true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto apply = [&](bool persist) {
        for (auto it = editors.cbegin(); it != editors.cend(); ++it) {
            if (auto *combo = qobject_cast<QComboBox *>(it.value())) values[it.key()] = combo->currentText();
            else if (auto *box = qobject_cast<QCheckBox *>(it.value())) values[it.key()] = box->isChecked() ? "true" : "false";
            else values[it.key()] = qobject_cast<QLineEdit *>(it.value())->text();
        }
        Profile checked; QString error;
        if (!Profile::fromValues(settings.name, values, checked, error)) {
            QMessageBox::warning(&dialog, "Invalid settings", error); return;
        }
        if (persist && !config->saveProfile(checked, &dialog)) return;
        QMetaObject::invokeMethod(worker, &SessionWorker::pause, Qt::QueuedConnection);
        render(); terminal->finish();
        settings = checked; terminal->setLimits(settings.terminalRows, settings.lineColumns);
        autoReconnect->blockSignals(true); autoReconnect->setChecked(settings.autoReconnect); autoReconnect->blockSignals(false);
        logPath.clear(); logFault.clear(); updateLogStatus();
        renderView(); addEvent(persist
            ? "Profile saved to INI; all settings tabs persisted. Session paused; Connect / Resume."
            : "Settings applied to this session only (NOT saved). Session paused; Connect / Resume.");
        dialog.accept();
    };
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] { apply(false); });
    connect(save, &QPushButton::clicked, &dialog, [&] { apply(true); });
    dialog.exec();
}
void SessionWindow::connectSession() {
    if (!collect()) return;
    if (settings.port.isEmpty() && settings.usbSerial.isEmpty()) {
        addEvent("Select or type a port before connecting."); return;
    }
    // Lock immediately, before the queued worker request can race another UI edit.
    applyState(SessionState::Waiting);
    terminal->finish();
    const Profile p = runtimeProfile();
    logPath.clear(); logFault.clear(); updateLogStatus();
    const QString directory = QDir(QFileInfo(config->ini.path).absolutePath()).absoluteFilePath(p.logDirectory);
    QMetaObject::invokeMethod(worker, [worker = worker, p, directory] {
        worker->configure(p, directory); worker->resume();
    }, Qt::QueuedConnection);
}
void SessionWindow::showEsptool() {
    if (diagnosticsDialog) {
        diagnosticsDialog->raise();
        diagnosticsDialog->activateWindow();
        return;
    }
    if (!collect()) return;
    auto acquire = [this](QString &error) {
        bool prepared = false;
        QMetaObject::invokeMethod(worker, [&] { prepared = worker->prepareTool(error); },
                                  Qt::BlockingQueuedConnection);
        return prepared;
    };
    auto release = [this] {
        QMetaObject::invokeMethod(worker, &SessionWorker::finishTool,
                                  Qt::BlockingQueuedConnection);
    };
    auto *dialog = new EsptoolDialog(runtimeProfile(), acquire, release,
                                     config->ini.confirmReset, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    diagnosticsDialog = dialog;
    connect(dialog, &EsptoolDialog::pathChanged, this, [this](const QString &path) {
        config->ini.esptoolPath = path;
        settings.esptoolPath.clear();
    });
    connect(dialog, &EsptoolDialog::saveSettingsRequested, this, [this, dialog] {
        if (collect() && config->saveProfile(settings, dialog)) {
            dialog->settingsSaved(config->ini.path);
            addEvent("Global esptool path and profile settings saved to INI.");
        }
    });
    connect(dialog, &QObject::destroyed, this, [this] {
        diagnosticsDialog = nullptr;
        if (!shuttingDown) applyState(state);
    });
    for (QObject *editor : connectionEditors) {
        editor->setProperty("enabled", false);
        editor->setProperty("toolTip", "Close the esptool diagnostics window before changing ports.");
    }
    dialog->show();
}
void SessionWindow::sendMessage() {
    if (readOnly->isChecked() || settings.readOnly || state != SessionState::Connected) {
        addEvent("Send blocked: not connected or read-only."); return;
    }
    const QString text = sendText->currentText();
    QByteArray bytes;
    if (hexSend->isChecked()) {
        QString compact = text; compact.remove(QRegularExpression("\\s"));
        if (compact.size() % 2 || !QRegularExpression("^[0-9A-Fa-f]*$").match(compact).hasMatch()) {
            addEvent("Hex send expects complete hexadecimal byte pairs."); return;
        }
        bytes = QByteArray::fromHex(compact.toLatin1());
    } else bytes = text.toUtf8();
    if (newline->currentText() == "lf") bytes += '\n';
    else if (newline->currentText() == "cr") bytes += '\r';
    else if (newline->currentText() == "crlf") bytes += "\r\n";
    if (sendText->findText(text) >= 0) sendText->removeItem(sendText->findText(text));
    sendText->insertItem(0, text); while (sendText->count() > 100) sendText->removeItem(sendText->count() - 1);
    sendText->setCurrentText("");
    QMetaObject::invokeMethod(worker, [worker = worker, bytes] { worker->send(bytes); }, Qt::QueuedConnection);
}
void SessionWindow::render() {
    quint64 dropped = 0;
    const auto records = display->takeRecords(dropped);
    if (records.isEmpty() && !dropped) return;
    const int scroll = view->verticalScrollBar()->value();
    for (const auto &record : records) terminal->feed(record.bytes, record.received);
    if (dropped) {
        terminal->finish();
        terminal->feed(QString("\n[render queue dropped %1 bytes; raw logging is independent]\n").arg(dropped).toUtf8());
        addEvent(QString("Display overload: %1 bytes omitted from rendered view. Check raw log fault status.").arg(dropped));
    }
    renderView();
    view->verticalScrollBar()->setValue(follow->isChecked() ? view->verticalScrollBar()->maximum() : scroll);
}
void SessionWindow::renderView() {
    const auto cursor = view->textCursor();
    const int scroll = view->verticalScrollBar()->value();
    view->setExtraSelections({});
    if (terminalFilter->text().isEmpty() || !filterValid)
        view->setHtml(terminal->html(settings.terminalTimestamp, settings.timestampMilliseconds));
    else {
        const auto text = terminal->plainText();
        QStringList selected;
        QVector<bool> matched;
        int row = 0;
        for (const auto &line : text.split('\n')) {
            const bool match = matchesFilter(line);
            matched << match;
            if (match) selected << (settings.terminalTimestamp
                ? terminal->timestampPrefix(row, settings.timestampMilliseconds) + line : line);
            ++row;
        }
        filterStatus->setText(QString("%1 / %2 buffered rows match. Save profile to keep this filter.")
                              .arg(selected.size()).arg(terminal->rowCount()));
        if (!filterHighlight->isChecked()) view->setPlainText(selected.join('\n'));
        else {
            view->setHtml(terminal->html(settings.terminalTimestamp, settings.timestampMilliseconds));
            QList<QTextEdit::ExtraSelection> highlights;
            for (auto block = view->document()->begin(); block.isValid(); block = block.next()) {
                if (!matched.value(block.blockNumber())) continue;
                QTextEdit::ExtraSelection selection;
                selection.cursor = QTextCursor(block);
                selection.format.setBackground(QColor("#544500"));
                selection.format.setProperty(QTextFormat::FullWidthSelection, true);
                highlights << selection;
            }
            view->setExtraSelections(highlights);
        }
    }
    if (cursor.hasSelection()) {
        auto restored = view->textCursor();
        const int end = view->document()->characterCount() - 1;
        restored.setPosition(qMin(cursor.anchor(), end));
        restored.setPosition(qMin(cursor.position(), end), QTextCursor::KeepAnchor);
        view->setTextCursor(restored);
    }
    view->verticalScrollBar()->setValue(follow->isChecked() ? view->verticalScrollBar()->maximum() : scroll);
}
void SessionWindow::updateFilter() {
    auto cursor = view->textCursor(); cursor.clearSelection(); view->setTextCursor(cursor);
    QString error;
    filterValid = filterMode->currentIndex() == 0
        || expressionFilter.compile(terminalFilter->text(),
            filterCase->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive, error);
    saveFiltered->setEnabled(filterValid && !terminalFilter->text().isEmpty());
    terminalFilter->setPlaceholderText(filterMode->currentIndex() == 0
        ? "Literal text (raw logs unchanged)" : "\"ord??tex*detta\" && temp >= 20");
    filterStatus->setStyleSheet(filterValid ? "" : "QLabel { color:#d03030; font-weight:bold; }");
    filterStatus->setText(filterValid ? "No filter. Raw logs are unchanged."
                                     : error + " — filter not applied; showing all rows.");
    filterStatus->setVisible(!filterValid || !terminalFilter->text().isEmpty());
    renderView();
}
bool SessionWindow::matchesFilter(const QString &line) const {
    return filterMode->currentIndex() == 1 ? expressionFilter.matches(line)
        : line.contains(terminalFilter->text(), filterCase->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive);
}
QString SessionWindow::filteredText(const QString &text) const {
    if (filterMode->currentIndex() == 1) return expressionFilter.select(text);
    return filterTerminalLines(text, terminalFilter->text(),
        filterCase->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive);
}
void SessionWindow::exportTerminal(ExportMode mode) {
    if (mode == ExportMode::Filtered && !filterValid) {
        addEvent("Cannot export filtered rows: correct the filter syntax first."); return;
    }
    QString text;
    if (mode == ExportMode::Selection) {
        if (!view->textCursor().hasSelection()) { addEvent("Select terminal text before exporting."); return; }
        text = view->textCursor().selectedText().replace(QChar(0x2029), '\n').replace(QChar(0x2028), '\n');
    } else {
        render();
        text = terminal->plainText();
        if (mode == ExportMode::Filtered) text = filteredText(text);
    }
    const QString path = QFileDialog::getSaveFileName(this, "Save terminal text (UTF-8 snapshot)",
        QDir(QFileInfo(config->ini.path).absolutePath()).filePath("terminal-"
            + QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz") + ".txt"),
        "Text files (*.txt);;All files (*)");
    if (path.isEmpty()) return;
    QString error;
    if (!saveTerminalText(path, text, error)) {
        addEvent(error); QMessageBox::warning(this, "Export failed", error); return;
    }
    addEvent("Terminal snapshot saved: " + path);
}
void SessionWindow::addEvent(const QString &message) {
    if (message.startsWith("LOGGING STOPPED")) {
        logFault = message;
    }
    updateLogStatus();
    events->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss.zzz") + " " + message);
}
void SessionWindow::updateLogStatus() {
    logStatus->setStyleSheet(logFault.isEmpty() ? "" : "QLabel { color:#d03030; font-weight:bold; }");
    if (!logFault.isEmpty()) { logStatus->setText(logFault); return; }
    const auto resolved = runtimeProfile();
    const QString directory = QDir(QFileInfo(config->ini.path).absolutePath()).absoluteFilePath(resolved.logDirectory);
    logStatus->setToolTip(QString("Template: %1 | segment: %2 bytes / %3 s (0 = off) | keep %4 completed + active")
        .arg(settings.logName).arg(settings.logBytes).arg(settings.logSeconds).arg(settings.logRetention));
    if (!settings.logging) {
        logStatus->setText("Traffic logging OFF (enable under Settings > Logging). Folder: " + directory);
        return;
    }
    const bool recording = state == SessionState::Connected && !logPath.isEmpty();
    QString files = "Folder: " + directory;
    if (!logPath.isEmpty()) {
        files = "File: " + logPath;
        if (settings.logMode == "both")
            files += " + " + QFileInfo(logPath.chopped(settings.rawLogExtension.size()) + settings.logExtension).fileName();
    }
    logStatus->setText(QString("Traffic logging %1 (%2). %3")
        .arg(recording ? "ON" : "enabled, not recording",
             settings.logMode == "text" ? "Text" : settings.logMode == "raw" ? "Raw bytes" : "Text + Raw", files));
}
