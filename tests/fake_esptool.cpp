#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
#include <QThread>
#include <QtEndian>
#include <algorithm>

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
            || args[i].startsWith("read-") || args[i].startsWith("read_")
            || args[i].startsWith("write-") || args[i].startsWith("write_")) {
            commandIndex = i;
            break;
        }
    }
    if (commandIndex < 0) return 2;
    const QString command = args[commandIndex];
    if (command == "chip-id") {
        QTextStream(stdout) << (mode == "wrong-chip"
            ? "Chip type:          ESP32-C3 (revision v0.2)\n"
            : "Detecting chip type... ESP32-S3\n"
              "Chip type:          ESP32-S3 (QFN56) (revision v0.2)\n"
              "Warning: ESP32-S3 has no chip ID. Reading MAC address instead.\n");
        return 0;
    }
    if (command == "flash-id") {
        QTextStream(stdout) << "Manufacturer: fake\nDetected flash size: "
            << (mode == "wrong-flash-size" ? "8MB\n" : "4MB\n");
        return 0;
    }
    if (command == "write-flash" || command == "write_flash") return 0;
    if (command == "read-flash" && commandIndex + 3 < args.size()) {
        bool ok = false;
        const quint64 size = args[commandIndex + 2].toULongLong(&ok, 0);
        if (!ok) return 3;
        QFile output(args[commandIndex + 3]);
        if (!output.open(QIODevice::WriteOnly)) return 4;
        if (size == 8192 && mode == "ota-read-fail") return 7;
        QByteArray bytes(int(size), char(0xa5));
        if (mode.startsWith("ota") && size == 0xc00) {
            bytes.fill(char(0xff));
            const int count = mode == "ota-ambiguous" ? 4 : 3;
            for (int i = 0; i < count; ++i) {
                auto *entry = reinterpret_cast<uchar *>(bytes.data() + i * 32);
                std::fill(entry, entry + 32, 0);
                entry[0] = 0xaa; entry[1] = 0x50;
                const bool metadata = i == 0 || i == 3;
                entry[2] = metadata ? 1 : 0;
                entry[3] = metadata ? 0 : quint8(0x10 + i - 1);
                const quint32 offset = i == 0 ? 0xd000 : quint32(i * 0x10000);
                qToLittleEndian(offset, entry + 4);
                qToLittleEndian<quint32>(metadata ? 8192 : 65536, entry + 8);
                const QByteArray label = metadata ? QByteArray("otadata")
                    : QByteArray("ota_") + QByteArray::number(i - 1);
                std::copy(label.begin(), label.end(), entry + 12);
                if (metadata && mode == "ota-encrypted") qToLittleEndian<quint32>(1, entry + 28);
            }
        } else if (mode.startsWith("ota") && size == 8192) {
            bytes.fill(char(0xff));
            for (int i = 0; i < 2; ++i) {
                auto *entry = reinterpret_cast<uchar *>(bytes.data() + i * 4096);
                qToLittleEndian<quint32>(i + 1, entry);
                qToLittleEndian<quint32>(2, entry + 24);
                const quint32 crc = i == 0 ? 0x4743989a : 0x55f63774;
                qToLittleEndian<quint32>(mode == "ota-bad-crc" ? 0 : crc, entry + 28);
            }
            if (mode == "ota-short") bytes.chop(1);
        }
        if (output.write(bytes) != bytes.size()) return 5;
        QTextStream(stdout) << "Read " << size << " bytes\n";
        return 0;
    }
    return 2;
}
