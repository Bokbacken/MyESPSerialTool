#include "window.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QTimer>
#include <QStandardPaths>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("Serial ESP Helper");
    QCoreApplication::setApplicationVersion("0.25.1");
    qRegisterMetaType<Profile>();
    qRegisterMetaType<SessionState>();
    QCommandLineParser parser;
    parser.setApplicationDescription("Multi-window serial terminal; no network access.");
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({{"c", "config"}, "Explicit INI file path", "file"});
    parser.addOption({"smoke-test", "Open two disconnected windows, verify controls, then exit without saving"});
    parser.process(app);
    const bool smoke = parser.isSet("smoke-test");
    const QString defaultPath = QDir(QCoreApplication::applicationDirPath()).filePath("SerialESPHelper.ini");
    const QString path = parser.isSet("config") ? QFileInfo(parser.value("config")).absoluteFilePath() : defaultPath;
    AppConfig config;
    QString error;
    if (smoke) {
        config.ini.path = path;
        config.ini.profiles = {Profile{}};
    } else if (!config.ini.load(path, error)) {
        QMessageBox::critical(nullptr, "Invalid INI — no connection started", path + "\n" + error);
        return 2;
    }
    if (!smoke && !parser.isSet("config") && !QFileInfo::exists(path)
        && QFileInfo::exists(QDir(QCoreApplication::applicationDirPath()).filePath("installed.marker"))) {
        const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (documents.isEmpty()) {
            QMessageBox::critical(nullptr, "Documents folder unavailable",
                "Cannot determine the user's Documents folder. Select an explicit --config INI path.");
            return 2;
        }
        for (auto &profile : config.ini.profiles)
            profile.logDirectory = QDir(documents).filePath("SerialESPHelper/Logs");
    }
    if (!smoke && !QFileInfo::exists(path) && !config.ini.save(error)) {
        QMessageBox::warning(nullptr, "Default INI is not writable",
            error + "\nSelect an explicit configuration file; no silent relocation.");
        if (!config.chooseFile(nullptr)) return 2;
    }
    auto *first = new SessionWindow(&config, config.ini.profiles.first()); first->show();
    if (smoke) {
        auto *second = new SessionWindow(&config, config.ini.profiles.first()); second->show();
        QTimer::singleShot(250, &app, [&app, first, second] {
            first->close(); second->close(); app.exit(0);
        });
    }
    return app.exec();
}
