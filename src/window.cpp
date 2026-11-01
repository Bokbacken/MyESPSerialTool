#include "window.h"
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
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
#include <QTextEdit>
#include <QVBoxLayout>

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

SessionWindow::SessionWindow(AppConfig *c, const Profile &p)
    : config(c), settings(p), display(std::make_shared<DisplayBuffer>()),
      worker(new SessionWorker(display)) {
    setAttribute(Qt::WA_DeleteOnClose);
    resize(1050, 760);
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    auto *profileRow = new QHBoxLayout;
    profiles = new QComboBox;
    profiles->setObjectName("profiles");
    auto *newProfile = new QPushButton("New profile");
    auto *save = new QPushButton("Save profile");
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
    auto *advancedButton = new QPushButton("Advanced…");
    auto *connectButton = new QPushButton("Connect / Resume");
    auto *pauseButton = new QPushButton("Manual pause");
    connectionRow->addWidget(new QLabel("Port")); connectionRow->addWidget(port, 1);
    connectionRow->addWidget(new QLabel("Baud")); connectionRow->addWidget(baud);
    connectionRow->addWidget(readOnly); connectionRow->addWidget(advancedButton);
    connectionRow->addWidget(connectButton); connectionRow->addWidget(pauseButton);
    layout->addLayout(connectionRow);
    auto *identityRow = new QHBoxLayout;
    identity = new QLabel; identity->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *forget = new QPushButton("Forget USB binding");
    identityRow->addWidget(identity, 1); identityRow->addWidget(forget);
    layout->addLayout(identityRow);
    status = new QLabel("Disconnected"); status->setObjectName("sessionState");
    layout->addWidget(status);
    logStatus = new QLabel("Raw logging disabled"); logStatus->setObjectName("logStatus");
    logStatus->setWordWrap(true); layout->addWidget(logStatus);
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
    setCentralWidget(central);
    refreshProfiles(); loadProfile(p);
    worker->moveToThread(&thread);
    connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &SessionWorker::stateChanged, this, [this](SessionState s) {
        state = s; status->setText(stateName(s));
        sendButton->setEnabled(s == SessionState::Connected && !settings.readOnly);
        if (s != SessionState::Connected && s != SessionState::Connecting) {
            render(); terminal->finish(); view->setHtml(terminal->html());
        }
    });
    connect(worker, &SessionWorker::problem, this, &SessionWindow::addEvent);
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
            terminal->localEcho(bytes); view->setHtml(terminal->html());
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
    connect(advancedButton, &QPushButton::clicked, this, &SessionWindow::advanced);
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
    connect(clear, &QPushButton::clicked, this, [this] { terminal->clear(); view->clear(); });
    connect(sendButton, &QPushButton::clicked, this, &SessionWindow::sendMessage);
    connect(sendText->lineEdit(), &QLineEdit::returnPressed, this, &SessionWindow::sendMessage);
    connect(readOnly, &QCheckBox::toggled, this, [this](bool checked) {
        sendButton->setEnabled(state == SessionState::Connected && !checked && !settings.readOnly);
        if (checked != settings.readOnly && state == SessionState::Connected) {
            QMetaObject::invokeMethod(worker, &SessionWorker::pause, Qt::QueuedConnection);
            addEvent("Read-only mode changed: paused. Connect to apply the new open mode.");
        }
    });
    renderTimer = new QTimer(this); renderTimer->setInterval(50);
    connect(renderTimer, &QTimer::timeout, this, &SessionWindow::render); renderTimer->start();
    connect(&thread, &QThread::started, worker, &SessionWorker::monitor);
    thread.start();
    addEvent("Stage 1: event-driven serial. Manual pause releases the port and suppresses reconnect.");
    addEvent("Read-only never calls DTR/RTS setters; OS/adapter opening may still affect physical lines.");
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
    echo->setChecked(p.localEcho); follow->setChecked(p.follow); newline->setCurrentText(p.newline);
    terminal = std::make_unique<Terminal>(p.terminalRows, p.lineColumns);
    quint64 dropped; display->take(dropped); view->clear(); sendText->clear(); sendButton->setEnabled(false);
    identity->setText(p.usbSerial.isEmpty() ? "No stable USB binding; only the explicit selected port is used."
        : QString("Bound USB: %1 VID=%2 PID=%3 (port field cannot override this binding)")
            .arg(p.usbSerial).arg(p.vid).arg(p.pid));
    setWindowTitle("Serial ESP Helper — " + p.name);
}
bool SessionWindow::collect() {
    auto values = settings.values();
    values["port"] = port->currentText(); values["baud"] = baud->currentText();
    values["readOnly"] = readOnly->isChecked() ? "true" : "false";
    values["localEcho"] = echo->isChecked() ? "true" : "false";
    values["follow"] = follow->isChecked() ? "true" : "false";
    values["newline"] = newline->currentText();
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
void SessionWindow::advanced() {
    if (!collect()) return;
    QDialog dialog(this); dialog.setWindowTitle("Advanced profile settings (apply on Connect)");
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    QMap<QString, QWidget *> editors;
    auto values = settings.values();
    const QMap<QString, QStringList> options = {
        {"dataBits", {"5", "6", "7", "8"}}, {"parity", {"none", "even", "odd", "space", "mark"}},
        {"stopBits", {"1", "1.5", "2"}}, {"flowControl", {"none", "hardware", "software"}}};
    const QStringList flags = {"dtr", "rts", "logging", "timestampMetadata"};
    const QList<QPair<QString, QString>> fields = {
        {"dataBits", "Data bits"}, {"parity", "Parity"}, {"stopBits", "Stop bits"},
        {"flowControl", "Flow control"}, {"dtr", "DTR (not set in read-only)"},
        {"rts", "RTS (not set in read-only / hardware flow)"},
        {"writeTimeoutMs", "Write completion timeout (100–60000 ms)"},
        {"retryLimit", "Open retries per outage (1–100, at 1 Hz)"},
        {"terminalRows", "Maximum terminal rows (10–10000)"},
        {"lineColumns", "Wrap at columns (32–16384; total cells capped at 1 Mi)"},
        {"logging", "Exact raw receive logging"},
        {"logDirectory", "Log directory (relative to INI)"},
        {"logBytes", "Raw segment size (1–1073741824 bytes)"},
        {"logRetention", "Completed segments retained (1–100)"},
        {"logQueueBytes", "Disk queue byte limit (1–67108864)"},
        {"timestampMetadata", "Separate reception UTC metadata (not device time)"}};
    for (const auto &field : fields) {
        QWidget *editor;
        if (options.contains(field.first)) {
            auto *combo = new QComboBox; combo->addItems(options[field.first]);
            combo->setCurrentText(values[field.first]); editor = combo;
        } else if (flags.contains(field.first)) {
            auto *box = new QCheckBox; box->setChecked(values[field.first] == "true"); editor = box;
            if (field.first == "dtr" || field.first == "rts") box->setEnabled(!settings.readOnly);
        } else editor = new QLineEdit(values[field.first]);
        editors[field.first] = editor; form->addRow(field.second, editor);
    }
    auto *flow = qobject_cast<QComboBox *>(editors["flowControl"]);
    auto *rts = qobject_cast<QCheckBox *>(editors["rts"]);
    auto updateRts = [this, flow, rts] { rts->setEnabled(!settings.readOnly && flow->currentText() != "hardware"); };
    connect(flow, &QComboBox::currentTextChanged, &dialog, updateRts); updateRts();
    layout->addLayout(form);
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
        settings = checked; terminal = std::make_unique<Terminal>(settings.terminalRows, settings.lineColumns);
        view->clear(); addEvent("Advanced settings updated; paused. Connect to apply, Save profile to persist.");
        dialog.accept();
    });
    dialog.exec();
}
void SessionWindow::connectSession() {
    if (!collect()) return;
    if (settings.port.isEmpty() && settings.usbSerial.isEmpty()) {
        addEvent("Select or type a port before connecting."); return;
    }
    terminal->finish();
    const Profile p = settings;
    if (!p.logging) { logStatus->setStyleSheet(""); logStatus->setText("Raw logging disabled"); }
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
    view->setHtml(terminal->html());
    view->verticalScrollBar()->setValue(follow->isChecked() ? view->verticalScrollBar()->maximum() : scroll);
}
void SessionWindow::addEvent(const QString &message) {
    if (message.startsWith("LOGGING STOPPED")) {
        logStatus->setText(message);
        logStatus->setStyleSheet("QLabel { color:#d03030; font-weight:bold; }");
    } else if (message.startsWith("Raw capture:")) {
        logStatus->setText("Exact raw receive logging enabled; capture path shown below. Metadata uses reception time.");
        logStatus->setStyleSheet("");
    }
    events->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss.zzz") + " " + message);
}
