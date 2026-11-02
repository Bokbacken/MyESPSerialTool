#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
#include <QThread>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    const QString recordPath = qEnvironmentVariable("FAKE_ESPTOOL_ARGS_FILE");
    if (!recordPath.isEmpty()) {
        QFile record(recordPath);
        if (record.open(QIODevice::WriteOnly | QIODevice::Append)) {
            QJsonArray array;
            for (const QString &arg : args) array.append(arg);
            record.write(QJsonDocument(array).toJson(QJsonDocument::Compact) + '\n');
        }
    }

    if (args == QStringList{"version"}) {
        QTextStream(stdout) << "esptool v5.1.0\n";
        return 0;
    }
    const QString mode = qEnvironmentVariable("FAKE_ESPTOOL_MODE");
    if (mode == "sleep") QThread::sleep(60);
    if (mode == "fail") {
        QTextStream(stderr) << "simulated esptool failure\n";
        return 7;
    }

    int commandIndex = -1;
    for (int i = 0; i < args.size(); ++i) {
        if (args[i].startsWith("chip-") || args[i].startsWith("chip_")
            || args[i].startsWith("flash-") || args[i].startsWith("flash_")
            || args[i].startsWith("read-") || args[i].startsWith("read_")) {
            commandIndex = i;
            break;
        }
    }
    if (commandIndex < 0) return 2;
    const QString command = args[commandIndex];
    if (command == "chip-id") {
        QTextStream(stdout) << "Chip is ESP32-S3, revision v0.2\n";
        return 0;
    }
    if (command == "flash-id") {
        QTextStream(stdout) << "Manufacturer: fake, detected size: 4MB\n";
        return 0;
    }
    if (command == "read-flash" && commandIndex + 3 < args.size()) {
        bool ok = false;
        const quint64 size = args[commandIndex + 2].toULongLong(&ok, 0);
        if (!ok) return 3;
        QFile output(args[commandIndex + 3]);
        if (!output.open(QIODevice::WriteOnly)) return 4;
        const QByteArray bytes(int(size), char(0xa5));
        if (output.write(bytes) != bytes.size()) return 5;
        QTextStream(stdout) << "Read " << size << " bytes\n";
        return 0;
    }
    return 2;
}
