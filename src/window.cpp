#include "window.h"
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

SessionWindow::SessionWindow(AppConfig *c, const Profile &p, SerialTransport *transport)
    : config(c), settings(p), display(std::make_shared<DisplayBuffer>()),
      worker(new SessionWorker(display, transport)) {
    setAttribute(Qt::WA_DeleteOnClose);
    resize(1050, 760);
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    auto *profileRow = new QHBoxLayout;
    profiles = new QComboBox;
    profiles->setObjectName("profiles");
    auto *newProfile = new QPushButton("New profile");
    newProfile->setObjectName("newProfile");
    auto *save = new QPushButton("Save profile");
    save->setObjectName("saveProfile");
    auto *newWindow = new QPushButton("New window");
    auto *chooseConfig = new QPushButton("Save INI as…");
    profileRow->addWidget(new QLabel("Profile")); profileRow->addWidget(profiles, 1);
    profileRow->addWidget(newProfile); profileRow->addWidget(save);
    profileRow->addWidget(newWindow); profileRow->addWidget(chooseConfig);
    layout->addLayout(profileRow);
    auto *connectionRow = new QHBoxLayout;
    port = new QComboBox; port->setEditable(true); port->setObjectName("port");
    baud = new QComboBox; baud->setEditable(true); baud->setObjectName("baud");
    for (int rate : {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600})
        baud->addItem(QString::number(rate));
    readOnly = new QCheckBox("Read-only");
    readOnly->setObjectName("readOnly");
    auto *advancedButton = new QPushButton("Settings…");
    advancedButton->setObjectName("advanced");
    auto *connectButton = new QPushButton("Connect / Resume");
    connectButton->setObjectName("connectButton");
    auto *pauseButton = new QPushButton("Manual pause");
    connectionRow->addWidget(new QLabel("Port")); connectionRow->addWidget(port, 1);
    connectionRow->addWidget(new QLabel("Baud")); connectionRow->addWidget(baud);
    connectionRow->addWidget(readOnly); connectionRow->addWidget(advancedButton);
    connectionRow->addWidget(connectButton); connectionRow->addWidget(pauseButton);
    layout->addLayout(connectionRow);
    autoReconnect = new QCheckBox("Reconnect automatically after connection loss");
    autoReconnect->setObjectName("autoReconnect");
    layout->addWidget(autoReconnect);
    auto *identityRow = new QHBoxLayout;
    identity = new QLabel; identity->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *forget = new QPushButton("Forget USB binding");
    forget->setObjectName("forgetBinding");
    connectionEditors = {profiles, newProfile, port, baud, forget, connectButton};
    identityRow->addWidget(identity, 1); identityRow->addWidget(forget);
    layout->addLayout(identityRow);
    status = new QLabel("Disconnected"); status->setObjectName("sessionState");
    layout->addWidget(status);
    logStatus = new QLabel("Raw logging disabled"); logStatus->setObjectName("logStatus");
    logStatus->setWordWrap(true); layout->addWidget(logStatus);
    logStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *logTools = new QHBoxLayout;
    auto *logSettings = new QPushButton("Logging settings…");
    logSettings->setObjectName("logSettings");
    auto *openLogs = new QPushButton("Open log folder");
    logTools->addWidget(logSettings); logTools->addWidget(openLogs); logTools->addStretch();
    layout->addLayout(logTools);
    connect(logSettings, &QPushButton::clicked, this, [this] { advanced(1); });
    connect(openLogs, &QPushButton::clicked, this, [this] {
        const auto directory = QDir(QFileInfo(config->ini.path).absolutePath()).absoluteFilePath(settings.logDirectory);
        if (!QDir().mkpath(directory) || !QDesktopServices::openUrl(QUrl::fromLocalFile(directory)))
            addEvent("Cannot open log folder: " + directory);
    });
    view = new QTextEdit; view->setReadOnly(true); view->setObjectName("terminal");
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    view->setLineWrapMode(QTextEdit::NoWrap);
    view->setStyleSheet("QTextEdit { background:#151515; color:#dddddd; }");
    layout->addWidget(view, 1);
    auto *tools = new QHBoxLayout;
    follow = new QCheckBox("Follow scroll (receiving never paused)");
    auto *clear = new QPushButton("Clear rendered view (raw logs unchanged)");
    tools->addWidget(follow); tools->addStretch(); tools->addWidget(clear);
    layout->addLayout(tools);
    auto *exportRow = new QHBoxLayout;
    terminalFilter = new QLineEdit;
    terminalFilter->setObjectName("terminalFilter"); terminalFilter->setMaxLength(4096);
    terminalFilter->setPlaceholderText("Show lines containing text (raw logs are unchanged)");
    filterCase = new QCheckBox("Match case");
    filterCase->setObjectName("filterCase");
    filterMode = new QComboBox;
    filterMode->setObjectName("filterMode"); filterMode->addItems({"Text", "Expression"});
    filterHighlight = new QCheckBox("Highlight matches");
    filterHighlight->setObjectName("filterHighlight");
    auto *filterHelp = new QPushButton("Filter help");
    auto *saveBuffer = new QPushButton("Save whole buffer…");
    saveBuffer->setObjectName("saveBuffer");
    auto *saveSelection = new QPushButton("Save selection…");
    saveSelection->setObjectName("saveSelection"); saveSelection->setEnabled(false);
    saveFiltered = new QPushButton("Save filtered rows…");
    saveFiltered->setObjectName("saveFiltered"); saveFiltered->setEnabled(false);
    auto *filterRow = new QHBoxLayout;
    filterRow->addWidget(filterMode); filterRow->addWidget(terminalFilter, 1);
    filterRow->addWidget(filterCase); filterRow->addWidget(filterHighlight); filterRow->addWidget(filterHelp);
    layout->addLayout(filterRow);
    filterStatus = new QLabel;
    filterStatus->setObjectName("filterStatus"); filterStatus->setWordWrap(true);
    layout->addWidget(filterStatus);
    exportRow->addStretch();
    exportRow->addWidget(saveBuffer); exportRow->addWidget(saveSelection); exportRow->addWidget(saveFiltered);
    layout->addLayout(exportRow);
    connect(terminalFilter, &QLineEdit::textChanged, this, [this] { updateFilter(); });
    connect(filterCase, &QCheckBox::toggled, this, [this] { updateFilter(); });
    connect(filterMode, &QComboBox::currentIndexChanged, this, [this] { updateFilter(); });
    connect(filterHighlight, &QCheckBox::toggled, this, [this] { updateFilter(); });
    connect(filterHelp, &QPushButton::clicked, this, [this] {
        QMessageBox help(this);
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
            "Save profile stores this filter. Invalid syntax disables filtered export.\n"
            "Highlight keeps all rows visible; filtered export still saves only matches.\n"
            "Filters/export use the retained terminal buffer, never change the raw log.\n"
            "Saved filters follow INI rules: no leading/trailing whitespace or whitespace before ;/#.\n"
            "Expressions: up to 4096 characters, 128 terms/operators, 32 nesting levels.");
        help.exec();
    });
    connect(view, &QTextEdit::selectionChanged, this, [this, saveSelection] {
        saveSelection->setEnabled(view->textCursor().hasSelection());
    });
    connect(saveBuffer, &QPushButton::clicked, this, [this] { exportTerminal(ExportMode::Buffer); });
    connect(saveSelection, &QPushButton::clicked, this, [this] { exportTerminal(ExportMode::Selection); });
    connect(saveFiltered, &QPushButton::clicked, this, [this] { exportTerminal(ExportMode::Filtered); });
    auto *sendRow = new QHBoxLayout;
    sendText = new QComboBox; sendText->setEditable(true); sendText->setObjectName("sendText");
    sendText->setInsertPolicy(QComboBox::NoInsert);
    sendText->lineEdit()->setMaxLength(32768);
    newline = new QComboBox; newline->addItems({"none", "lf", "cr", "crlf"});
    echo = new QCheckBox("Local echo"); hexSend = new QCheckBox("Hex bytes");
    hexSend->setToolTip("Pairs of hexadecimal digits, optional spaces. Newline is appended separately.");
    sendButton = new QPushButton("Send"); sendButton->setObjectName("sendButton");
    sendRow->addWidget(sendText, 1); sendRow->addWidget(newline); sendRow->addWidget(echo);
    sendRow->addWidget(hexSend); sendRow->addWidget(sendButton);
    layout->addLayout(sendRow);
    events = new QPlainTextEdit; events->setReadOnly(true);
    events->setMaximumHeight(110); events->setMaximumBlockCount(200);
    events->setObjectName("events");
    layout->addWidget(events);
    auto *configLabel = new QLabel("INI: " + config->ini.path);
    configLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(configLabel);
    auto *versionLabel = new QLabel("Serial ESP Helper — Version " + QCoreApplication::applicationVersion());
    versionLabel->setObjectName("applicationVersion");
    versionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(versionLabel);
    setCentralWidget(central);
    refreshProfiles(); loadProfile(p);
    worker->moveToThread(&thread);
    connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &SessionWorker::stateChanged, this, &SessionWindow::applyState);
    connect(worker, &SessionWorker::problem, this, &SessionWindow::addEvent);
    connect(worker, &SessionWorker::logFileChanged, this, [this](const QString &path) {
        logPath = path; updateLogStatus();
    });
    connect(worker, &SessionWorker::portsAvailable, this, [this](const QStringList &ports) {
        availablePorts = ports; refreshPorts();
    });
    connect(worker, &SessionWorker::identityBound, this, [this](const Profile &bound) {
        settings.usbSerial = bound.usbSerial; settings.vid = bound.vid; settings.pid = bound.pid;
        identity->setText(QString("Bound USB: %1 VID=%2 PID=%3 (Save profile to persist)")
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
    connect(config, &AppConfig::profilesChanged, this, [this, configLabel] {
        refreshProfiles(); configLabel->setText("INI: " + config->ini.path);
    });
    connect(profiles, &QComboBox::activated, this, [this](int index) {
        if (index >= 0 && index < config->ini.profiles.size()) {
            QMetaObject::invokeMethod(worker, &SessionWorker::pause, Qt::QueuedConnection);
            loadProfile(config->ini.profiles[index]);
        }
    });
    connect(newProfile, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const auto name = QInputDialog::getText(this, "New profile", "Unique name",
            QLineEdit::Normal, "", &ok);
        if (!ok) return;
        for (const auto &p : config->ini.profiles) if (p.name == name) {
            QMessageBox::warning(this, "Profile exists", "Select the existing profile instead."); return;
        }
        Profile p; p.name = name; QString error; Profile checked;
        if (!Profile::fromValues(name, p.values(), checked, error)) {
            QMessageBox::warning(this, "Invalid name", error); return;
        }
        QMetaObject::invokeMethod(worker, &SessionWorker::pause, Qt::QueuedConnection);
        loadProfile(p);
    });
    connect(save, &QPushButton::clicked, this, [this] {
        if (collect() && config->saveProfile(settings, this)) addEvent("Profile saved atomically; previous INI in .bak");
    });
    connect(newWindow, &QPushButton::clicked, this, [this] {
        if (collect()) (new SessionWindow(config, settings))->show();
    });
    connect(chooseConfig, &QPushButton::clicked, this, [this] { config->chooseFile(this); });
    connect(advancedButton, &QPushButton::clicked, this, [this] { advanced(); });
    connect(autoReconnect, &QCheckBox::toggled, this, [this](bool enabled) {
        settings.autoReconnect = enabled;
        QMetaObject::invokeMethod(worker, [worker = worker, enabled] { worker->setAutoReconnect(enabled); },
                                  Qt::QueuedConnection);
        addEvent(enabled ? "Automatic reconnect enabled. Save profile to persist."
                         : "Automatic reconnect disabled. Save profile to persist.");
    });
    connect(connectButton, &QPushButton::clicked, this, &SessionWindow::connectSession);
    connect(pauseButton, &QPushButton::clicked, this, [this] {
        QMetaObject::invokeMethod(worker, &SessionWorker::pause, Qt::QueuedConnection);
    });
    connect(forget, &QPushButton::clicked, this, [this] {
        QMetaObject::invokeMethod(worker, &SessionWorker::pause, Qt::QueuedConnection);
        settings.usbSerial.clear(); settings.vid = settings.pid = -1;
        identity->setText("No stable USB binding; only the explicit selected port is used.");
        addEvent("USB binding forgotten; session paused. Connect and Save to bind the new device.");
    });
    connect(clear, &QPushButton::clicked, this, [this] { terminal->clear(); renderView(); });
    connect(sendButton, &QPushButton::clicked, this, &SessionWindow::sendMessage);
    connect(sendText->lineEdit(), &QLineEdit::returnPressed, this, &SessionWindow::sendMessage);
    connect(readOnly, &QCheckBox::toggled, this, [this](bool checked) {
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
    addEvent("Stage 1: event-driven serial. Manual pause releases the port and suppresses reconnect.");
    addEvent("DTR/RTS are explicit connection settings, independent of read-only send gating.");
    addEvent("OS/adapter opening or closing may pulse physical lines. Read-only toggles never reopen the port.");
    if (p.autoConnect) QTimer::singleShot(0, this, &SessionWindow::connectSession);
}
void SessionWindow::applyState(SessionState s) {
    state = s; status->setText(stateName(s));
    updateLogStatus();
    const bool active = s == SessionState::Waiting || s == SessionState::Connecting
        || s == SessionState::Connected || s == SessionState::Loaned;
    for (QWidget *editor : connectionEditors) {
        editor->setEnabled(!active);
        editor->setToolTip(active ? "Manual pause before editing connection settings" : "");
    }
    sendButton->setEnabled(s == SessionState::Connected && !settings.readOnly);
    if (s != SessionState::Connected && s != SessionState::Connecting) {
        render(); terminal->finish(); renderView();
    }
}
SessionWindow::~SessionWindow() {
    renderTimer->stop();
    QMetaObject::invokeMethod(worker, &SessionWorker::shutdown, Qt::BlockingQueuedConnection);
    thread.quit(); thread.wait();
}
void SessionWindow::refreshProfiles() {
    profiles->blockSignals(true); profiles->clear();
    for (const Profile &p : config->ini.profiles) profiles->addItem(p.name);
    if (profiles->findText(settings.name) < 0) profiles->addItem(settings.name);
    profiles->setCurrentText(settings.name); profiles->blockSignals(false);
}
void SessionWindow::loadProfile(const Profile &p) {
    settings = p;
    if (profiles->findText(p.name) < 0) profiles->addItem(p.name);
    profiles->setCurrentText(p.name);
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
    setWindowTitle("Serial ESP Helper " + QCoreApplication::applicationVersion() + " — " + p.name);
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
    QDialog dialog(this); dialog.setWindowTitle("Profile settings");
    dialog.resize(780, 600);
    auto *layout = new QVBoxLayout(&dialog);
    auto *notice = new QLabel("OK applies settings and pauses this session. Connect / Resume to restart; Save profile to persist.");
    notice->setWordWrap(true); layout->addWidget(notice);
    auto *tabs = new QTabWidget;
    QList<QFormLayout *> forms;
    for (const auto &name : {"Connection", "Logging", "Terminal"}) {
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
        {"stopBits", {"1", "1.5", "2"}}, {"flowControl", {"none", "hardware", "software"}}};
    const QStringList flags = {"dtr", "rts", "logging", "timestampMetadata", "autoConnect", "textLog",
                               "autoReconnect", "newLogOnReconnect"};
    const QList<QPair<QString, QString>> fields = {
        {"dataBits", "Data bits"}, {"parity", "Parity"}, {"stopBits", "Stop bits"},
        {"autoConnect", "Connect on window open and retry failed initial opens (explicit opt-in)"},
        {"autoReconnect", "Reconnect this session after connection loss"},
        {"flowControl", "Flow control"},         {"dtr", "DTR (applied on open, independent of read-only)"},
        {"rts", "RTS (applied on open unless hardware flow manages it)"},
        {"writeTimeoutMs", "Write completion timeout (100–60000 ms)"},
        {"retryLimit", "Open retries per outage (1–100, at 1 Hz)"},
        {"terminalRows", "Maximum terminal rows (10–10000)"},
        {"lineColumns", "Wrap at columns (32–16384; total cells capped at 1 Mi)"},
        {"displayQueueBytes", "Display receive queue (1024–67108864 bytes; separate from disk log)"},
        {"logging", "Exact raw receive logging (session metadata is always recorded)"},
        {"logDirectory", "Log directory (relative to INI)"},
        {"logName", "Filename template (without extension)"},
        {"logBytes", "Raw segment size (1–1073741824 bytes)"},
        {"logSeconds", "Rotate after seconds (0 = size only; max 31536000)"},
        {"logRetention", "Rolling buffer: completed segments kept (1–100), plus active"},
        {"newLogOnReconnect", "New log set on reconnect (off = append to current log)"},
        {"logQueueBytes", "Disk queue byte limit (1–67108864)"},
        {"timestampMetadata", "Separate reception UTC raw-offset metadata (not device time)"},
        {"textLog", "Optional readable timestamped UTF-8 .text (with raw logging)"}};
    for (const auto &field : fields) {
        QWidget *editor;
        if (options.contains(field.first)) {
            auto *combo = new QComboBox; combo->addItems(options[field.first]);
            combo->setCurrentText(values[field.first]); editor = combo;
        } else if (flags.contains(field.first)) {
            auto *box = new QCheckBox; box->setChecked(values[field.first] == "true"); editor = box;
        } else editor = new QLineEdit(values[field.first]);
        editor->setObjectName(field.first);
        int group = 0;
        if (field.first.startsWith("log") || field.first == "newLogOnReconnect"
            || field.first == "timestampMetadata" || field.first == "textLog") group = 1;
        if (field.first == "terminalRows" || field.first == "lineColumns"
            || field.first == "displayQueueBytes") group = 2;
        editors[field.first] = editor; forms[group]->addRow(field.second, editor);
    }
    auto *namingHelp = new QLabel("Fields: {date}, {time} (UTC), {profile}, {port}, {serial}, {id}.\n"
        "Example: {profile}-{date}-{time}. A unique ID is always included to avoid overwriting.\n"
        "Rotation uses .raw.1, .raw.2, etc.; oldest segments of this log set are deleted.\n"
        "Size/time limits: whichever comes first. Time rotation happens on the next received bytes.\n"
        "Other log sets are never deleted automatically. Traffic logging captures received bytes only.");
    namingHelp->setWordWrap(true); forms[1]->addRow(namingHelp);
    auto *browse = new QPushButton("Choose log folder…");
    forms[1]->addRow(browse);
    connect(browse, &QPushButton::clicked, &dialog, [&] {
        auto *directory = qobject_cast<QLineEdit *>(editors["logDirectory"]);
        const QString selected = QFileDialog::getExistingDirectory(&dialog, "Log folder",
            QDir(QFileInfo(config->ini.path).absolutePath()).absoluteFilePath(directory->text()));
        if (!selected.isEmpty()) directory->setText(selected);
    });
    auto *flow = qobject_cast<QComboBox *>(editors["flowControl"]);
    auto *rts = qobject_cast<QCheckBox *>(editors["rts"]);
    auto updateRts = [flow, rts] { rts->setEnabled(flow->currentText() != "hardware"); };
    connect(flow, &QComboBox::currentTextChanged, &dialog, updateRts); updateRts();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        for (auto it = editors.cbegin(); it != editors.cend(); ++it) {
            if (auto *combo = qobject_cast<QComboBox *>(it.value())) values[it.key()] = combo->currentText();
            else if (auto *box = qobject_cast<QCheckBox *>(it.value())) values[it.key()] = box->isChecked() ? "true" : "false";
            else values[it.key()] = qobject_cast<QLineEdit *>(it.value())->text();
        }
        Profile checked; QString error;
        if (!Profile::fromValues(settings.name, values, checked, error)) {
            QMessageBox::warning(&dialog, "Invalid settings", error); return;
        }
        QMetaObject::invokeMethod(worker, &SessionWorker::pause, Qt::QueuedConnection);
        render(); terminal->finish();
        settings = checked; terminal->setLimits(settings.terminalRows, settings.lineColumns);
        autoReconnect->blockSignals(true); autoReconnect->setChecked(settings.autoReconnect); autoReconnect->blockSignals(false);
        logPath.clear(); logFault.clear(); updateLogStatus();
        renderView(); addEvent("Settings updated; paused. Connect to apply, Save profile to persist.");
        dialog.accept();
    });
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
    const Profile p = settings;
    logPath.clear(); logFault.clear(); updateLogStatus();
    const QString directory = QDir(QFileInfo(config->ini.path).absolutePath()).absoluteFilePath(p.logDirectory);
    QMetaObject::invokeMethod(worker, [worker = worker, p, directory] {
        worker->configure(p, directory); worker->resume();
    }, Qt::QueuedConnection);
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
    const auto bytes = display->take(dropped);
    if (bytes.isEmpty() && !dropped) return;
    const int scroll = view->verticalScrollBar()->value();
    terminal->feed(bytes);
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
    if (terminalFilter->text().isEmpty() || !filterValid) view->setHtml(terminal->html());
    else {
        const auto text = terminal->plainText();
        QStringList selected;
        QVector<bool> matched;
        for (const auto &line : text.split('\n')) {
            const bool match = matchesFilter(line);
            matched << match;
            if (match) selected << line;
        }
        filterStatus->setText(QString("%1 / %2 buffered rows match. Save profile to keep this filter.")
                              .arg(selected.size()).arg(terminal->rowCount()));
        if (!filterHighlight->isChecked()) view->setPlainText(selected.join('\n'));
        else {
            view->setHtml(terminal->html());
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
    const QString directory = QDir(QFileInfo(config->ini.path).absolutePath()).absoluteFilePath(settings.logDirectory);
    if (!settings.logging) {
        logStatus->setText("Traffic logging OFF. Enable it in Logging settings. Session metadata only.\nFolder: " + directory);
    } else {
        const bool recording = state == SessionState::Connected && !logPath.isEmpty();
        logStatus->setText(QString("Traffic logging %1 (received bytes). %2\n%3")
            .arg(recording ? "ON" : "enabled, not currently recording",
                 logPath.isEmpty() ? "Folder: " + directory : "File: " + logPath,
                 QString("Template: %1 | segment: %2 bytes / %3 s (0 = off) | keep %4 completed + active")
                    .arg(settings.logName).arg(settings.logBytes).arg(settings.logSeconds).arg(settings.logRetention)));
    }
}
