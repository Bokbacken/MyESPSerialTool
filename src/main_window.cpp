#include "main_window.h"
#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>

MainWindow::MainWindow(AppConfig *c, QWidget *parent) : QMainWindow(parent), config(c) {
    setObjectName("mainWindow");
    resize(1200, 820);
    area = new QMdiArea;
    area->setObjectName("sessionArea");
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    area->setTabsClosable(true);
    area->setTabsMovable(true);
    setCentralWidget(area);

    fileMenu = menuBar()->addMenu("&File");
    editMenu = menuBar()->addMenu("&Edit");
    viewMenu = menuBar()->addMenu("&View");
    sessionMenu = menuBar()->addMenu("&Session");
    espMenu = menuBar()->addMenu("&ESP");
    settingsMenu = menuBar()->addMenu("Se&ttings");
    windowMenu = menuBar()->addMenu("&Window");
    helpMenu = menuBar()->addMenu("&Help");

    openProfileMenu = new QMenu("&Open profile", this);
    openProfileMenu->setObjectName("openProfileMenu");
    exportMenu = new QMenu("&Export terminal text", this);
    newProfileAction = new QAction("&New profile…", this);
    newProfileAction->setObjectName("newProfile");
    newProfileAction->setShortcut(QKeySequence::New);
    saveIniAction = new QAction("Save INI &as…", this);
    closeSessionAction = new QAction("&Close session", this);
    closeSessionAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
    exitAction = new QAction("E&xit", this);
    exitAction->setShortcut(QKeySequence::Quit);
    globalSettingsAction = new QAction("&Global settings…", this);
    globalSettingsAction->setObjectName("globalSettingsAction");
    tileAction = new QAction("&Tile", this);
    cascadeAction = new QAction("&Cascade", this);
    tabbedAction = new QAction("Show as ta&bs", this);
    tabbedAction->setCheckable(true);
    nextAction = new QAction("&Next session", this);
    nextAction->setShortcut(QKeySequence::NextChild);
    previousAction = new QAction("&Previous session", this);
    previousAction->setShortcut(QKeySequence::PreviousChild);
    filterHelpAction = new QAction("&Filter syntax", this);
    aboutAction = new QAction("&About Serial ESP Helper", this);

    connect(openProfileMenu, &QMenu::aboutToShow, this, &MainWindow::populateProfiles);
    connect(windowMenu, &QMenu::aboutToShow, this, &MainWindow::populateWindows);
    connect(newProfileAction, &QAction::triggered, this, &MainWindow::newProfile);
    connect(saveIniAction, &QAction::triggered, this, [this] { config->chooseFile(this); });
    connect(closeSessionAction, &QAction::triggered, area, &QMdiArea::closeActiveSubWindow);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    connect(globalSettingsAction, &QAction::triggered, this, [this] { config->editGlobalSettings(this); });
    connect(tileAction, &QAction::triggered, this, [this] {
        tabbedAction->setChecked(false); area->tileSubWindows();
    });
    connect(cascadeAction, &QAction::triggered, this, [this] {
        tabbedAction->setChecked(false); area->cascadeSubWindows();
    });
    connect(tabbedAction, &QAction::toggled, this, [this](bool tabs) {
        area->setViewMode(tabs ? QMdiArea::TabbedView : QMdiArea::SubWindowView);
    });
    connect(nextAction, &QAction::triggered, area, &QMdiArea::activateNextSubWindow);
    connect(previousAction, &QAction::triggered, area, &QMdiArea::activatePreviousSubWindow);
    connect(filterHelpAction, &QAction::triggered, this, [this] { showFilterHelp(this); });
    connect(aboutAction, &QAction::triggered, this, [this] {
        QMessageBox::about(this, "About Serial ESP Helper", QString(
            "Serial ESP Helper %1\n\nSerial terminal, logging and guarded esptool diagnostics/flashing "
            "for ESP devices. No network access.\n\nINI file: %2\nQt %3")
            .arg(QCoreApplication::applicationVersion(), config->ini.path, qVersion()));
    });
    connect(area, &QMdiArea::subWindowActivated, this, [this](QMdiSubWindow *window) {
        if (!window && area->currentSubWindow()) return;  // application deactivated; keep menus
        rebuildMenus();
    });

    iniLabel = new QLabel;
    iniLabel->setObjectName("iniLocation");
    iniLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    versionLabel = new QLabel("Version " + QCoreApplication::applicationVersion());
    versionLabel->setObjectName("applicationVersion");
    statusBar()->addPermanentWidget(iniLabel);
    statusBar()->addPermanentWidget(versionLabel);
    auto updateIni = [this] { iniLabel->setText("INI: " + config->ini.path); };
    connect(config, &AppConfig::profilesChanged, this, updateIni);
    updateIni();
    rebuildMenus();
}

SessionWindow *MainWindow::activeSession() const {
    auto *window = area->currentSubWindow();
    return window ? qobject_cast<SessionWindow *>(window->widget()) : nullptr;
}

QList<SessionWindow *> MainWindow::sessions() const {
    QList<SessionWindow *> result;
    for (auto *window : area->subWindowList())
        if (auto *session = qobject_cast<SessionWindow *>(window->widget())) result << session;
    return result;
}

SessionWindow *MainWindow::openSession(const Profile &profile, SerialTransport *transport, bool reuseOpen) {
    if (reuseOpen) {
        for (auto *window : area->subWindowList()) {
            auto *session = qobject_cast<SessionWindow *>(window->widget());
            if (session && session->profileName() == profile.name) {
                area->setActiveSubWindow(window);
                return session;
            }
        }
    }
    auto *session = new SessionWindow(config, profile, transport);
    auto *window = area->addSubWindow(session);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setWindowTitle(session->windowTitle());
    connect(session, &SessionWindow::summaryChanged, this, [this, session] {
        if (session == activeSession()) updateTitle();
    });
    if (area->subWindowList().size() == 1 && area->viewMode() == QMdiArea::SubWindowView) window->showMaximized();
    else window->show();
    area->setActiveSubWindow(window);
    rebuildMenus();
    return session;
}

void MainWindow::rebuildMenus() {
    SessionWindow *session = activeSession();
    if (current && current != session) current->setShortcutsActive(false);
    current = session;
    if (session) session->setShortcutsActive(true);
    auto act = [session](const char *name) { return session ? session->action(name) : nullptr; };
    auto add = [](QMenu *menu, QAction *action) { if (action) menu->addAction(action); };

    fileMenu->clear();
    fileMenu->addMenu(openProfileMenu);
    fileMenu->addAction(newProfileAction);
    fileMenu->addSeparator();
    add(fileMenu, act("saveProfile"));
    fileMenu->addAction(saveIniAction);
    exportMenu->clear();
    for (const char *name : {"saveBuffer", "saveSelection", "saveFiltered"}) add(exportMenu, act(name));
    if (session) {
        fileMenu->addSeparator();
        fileMenu->addMenu(exportMenu);
        add(fileMenu, act("openLogFolder"));
    }
    fileMenu->addSeparator();
    closeSessionAction->setEnabled(session);
    fileMenu->addAction(closeSessionAction);
    fileMenu->addAction(exitAction);

    editMenu->clear();
    for (const char *name : {"copyText", "selectAll"}) add(editMenu, act(name));
    editMenu->addSeparator();
    for (const char *name : {"findFilter", "clearView"}) add(editMenu, act(name));

    viewMenu->clear();
    add(viewMenu, act("follow"));
    viewMenu->addSeparator();
    for (const char *name : {"showFilterBar", "showSendBar", "showEvents"}) add(viewMenu, act(name));

    sessionMenu->clear();
    for (const char *name : {"connectButton", "pauseButton", "splitLog"}) add(sessionMenu, act(name));
    sessionMenu->addSeparator();
    for (const char *name : {"readOnly", "autoReconnect", "forgetBinding"}) add(sessionMenu, act(name));

    espMenu->clear();
    for (const char *name : {"esptoolDiagnostics", "flashFirmware"}) add(espMenu, act(name));

    settingsMenu->clear();
    if (session) {
        auto *profileSection = settingsMenu->addSection("Profile: " + session->profileName());
        Q_UNUSED(profileSection);
        for (const char *name : {"advanced", "logSettings", "terminalSettings", "espSettings"})
            add(settingsMenu, act(name));
        settingsMenu->addSeparator();
    }
    settingsMenu->addAction(globalSettingsAction);

    helpMenu->clear();
    helpMenu->addAction(filterHelpAction);
    helpMenu->addSeparator();
    helpMenu->addAction(aboutAction);

    for (QMenu *menu : {editMenu, viewMenu, sessionMenu, espMenu}) menu->menuAction()->setVisible(session);
    populateWindows();
    updateTitle();
}

void MainWindow::populateProfiles() {
    openProfileMenu->clear();
    QStringList open;
    for (auto *session : sessions()) open << session->profileName();
    for (const Profile &profile : config->ini.profiles) {
        auto *item = openProfileMenu->addAction(profile.name);
        item->setCheckable(true);
        item->setChecked(open.contains(profile.name));
        connect(item, &QAction::triggered, this, [this, profile] { openSession(profile); });
    }
    if (config->ini.profiles.isEmpty()) openProfileMenu->addAction("(no saved profiles)")->setEnabled(false);
}

void MainWindow::populateWindows() {
    windowMenu->clear();
    qDeleteAll(windowMenu->findChildren<QActionGroup *>(Qt::FindDirectChildrenOnly));
    const bool any = !area->subWindowList().isEmpty();
    for (QAction *action : {tileAction, cascadeAction}) { action->setEnabled(any); windowMenu->addAction(action); }
    windowMenu->addAction(tabbedAction);
    windowMenu->addSeparator();
    nextAction->setEnabled(area->subWindowList().size() > 1);
    previousAction->setEnabled(area->subWindowList().size() > 1);
    windowMenu->addAction(nextAction);
    windowMenu->addAction(previousAction);
    if (any) windowMenu->addSeparator();
    auto *group = new QActionGroup(windowMenu);
    int index = 1;
    for (auto *window : area->subWindowList()) {
        auto *item = windowMenu->addAction(QString("&%1 %2").arg(index++).arg(window->windowTitle()));
        item->setCheckable(true);
        item->setChecked(window == area->currentSubWindow());
        group->addAction(item);
        connect(item, &QAction::triggered, this, [this, window] { area->setActiveSubWindow(window); });
    }
}

void MainWindow::newProfile() {
    bool ok = false;
    const auto name = QInputDialog::getText(this, "New profile", "Unique profile name",
                                            QLineEdit::Normal, "", &ok);
    if (!ok) return;
    for (auto *window : area->subWindowList()) {
        auto *session = qobject_cast<SessionWindow *>(window->widget());
        if (session && session->profileName() == name) {
            area->setActiveSubWindow(window);
            QMessageBox::warning(this, "Profile is open", "A session with this profile name is already open.");
            return;
        }
    }
    for (const auto &existing : config->ini.profiles) if (existing.name == name) {
        QMessageBox::warning(this, "Profile exists", "Use File > Open profile to open the existing profile.");
        return;
    }
    Profile profile; profile.name = name;
    Profile checked; QString error;
    if (!Profile::fromValues(name, profile.values(), checked, error)) {
        QMessageBox::warning(this, "Invalid name", error); return;
    }
    openSession(checked, nullptr, false);
    statusBar()->showMessage("New profile is not saved until File > Save profile.", 8000);
}

void MainWindow::updateTitle() {
    auto *session = activeSession();
    setWindowTitle("Serial ESP Helper " + QCoreApplication::applicationVersion()
                   + (session ? " — " + session->windowTitle() : QString()));
}

void MainWindow::closeEvent(QCloseEvent *event) {
    area->closeAllSubWindows();
    if (!area->subWindowList().isEmpty()) { event->ignore(); return; }
    event->accept();
}
