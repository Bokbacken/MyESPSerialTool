#pragma once
#include "window.h"
#include <QMainWindow>
#include <QPointer>

class QLabel;
class QMdiArea;
class QMdiSubWindow;
class QMenu;

// Application shell: standard menus, one MDI sub-window per profile session.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(AppConfig *config, QWidget *parent = nullptr);
    SessionWindow *openSession(const Profile &profile, SerialTransport *transport = nullptr,
                               bool reuseOpen = true);
    SessionWindow *activeSession() const;
    QList<SessionWindow *> sessions() const;
    QMdiArea *mdiArea() const { return area; }
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    AppConfig *config;
    QMdiArea *area;
    QMenu *fileMenu, *editMenu, *viewMenu, *sessionMenu, *espMenu, *settingsMenu, *windowMenu, *helpMenu;
    QMenu *openProfileMenu, *exportMenu;
    QAction *newProfileAction, *saveIniAction, *closeSessionAction, *exitAction, *globalSettingsAction;
    QAction *tileAction, *cascadeAction, *tabbedAction, *nextAction, *previousAction;
    QAction *filterHelpAction, *aboutAction;
    QPointer<SessionWindow> current;
    QLabel *iniLabel, *versionLabel;
    void rebuildMenus();
    void populateProfiles();
    void populateWindows();
    void newProfile();
    void updateTitle();
};
