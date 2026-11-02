#include "esptool.h"
#include <QRegularExpression>

namespace Esptool {

bool parseVersion(const QString &output, Version &version, QString &error) {
    const auto match = QRegularExpression(
        "esptool(?:\\.py)?\\s+v?(\\d+)\\.(\\d+)(?:\\.(\\d+))?",
        QRegularExpression::CaseInsensitiveOption)
        .match(output);
    if (!match.hasMatch()) {
        error = "Could not identify an esptool semantic version in its output.";
        return false;
    }
    Version parsed;
    parsed.major = match.captured(1).toInt();
    parsed.minor = match.captured(2).toInt();
    parsed.patch = match.captured(3).isEmpty() ? 0 : match.captured(3).toInt();
    parsed.text = match.captured(0);
    if (parsed.major < 4 || parsed.major > 5) {
        error = "Unsupported esptool version. This build supports official esptool 4.x and 5.x.";
        return false;
    }
    version = parsed;
    return true;
}

QStringList versionArguments() {
    return {"version"};
}

QStringList arguments(const Version &version, const QString &port, int baud,
                      Command command, const QStringList &extra) {
    QString name;
    if (version.major >= 5) {
        switch (command) {
        case Command::ChipId: name = "chip-id"; break;
        case Command::FlashId: name = "flash-id"; break;
        case Command::ReadFlash: name = "read-flash"; break;
        }
    } else {
        switch (command) {
        case Command::ChipId: name = "chip_id"; break;
        case Command::FlashId: name = "flash_id"; break;
        case Command::ReadFlash: name = "read_flash"; break;
        }
    }
    QStringList result{"--port", port, "--baud", QString::number(baud), name};
    result.append(extra);
    return result;
}

bool parsePartitionTable(const QByteArray &bytes, QVector<Partition> &partitions,
                         QString &error) {
    constexpr int entrySize = 32;
    QVector<Partition> parsed;
    bool foundEnd = false;
    for (int position = 0; position + entrySize <= bytes.size(); position += entrySize) {
        const auto *entry = reinterpret_cast<const quint8 *>(bytes.constData() + position);
        bool erased = true;
        for (int i = 0; i < entrySize; ++i) erased = erased && entry[i] == 0xff;
        if (erased || (entry[0] == 0xeb && entry[1] == 0xeb)) {
            foundEnd = true;
            break;
        }
        if (entry[0] != 0xaa || entry[1] != 0x50) {
            error = QString("Invalid partition entry magic at table offset 0x%1.")
                .arg(position, 0, 16);
            return false;
        }
        const auto read32 = [&](int offset) {
            return quint32(entry[offset]) | (quint32(entry[offset + 1]) << 8)
                | (quint32(entry[offset + 2]) << 16) | (quint32(entry[offset + 3]) << 24);
        };
        Partition partition;
        partition.type = entry[2];
        partition.subtype = entry[3];
        partition.offset = read32(4);
        partition.size = read32(8);
        int labelLength = 0;
        while (labelLength < 16 && entry[12 + labelLength] != 0
               && entry[12 + labelLength] != 0xff) ++labelLength;
        partition.label = QString::fromLatin1(
            reinterpret_cast<const char *>(entry + 12), labelLength);
        partition.flags = read32(28);
        if (partition.size == 0 || quint64(partition.offset) + partition.size > quint64(0x100000000)) {
            error = QString("Invalid size or address in partition '%1'.").arg(partition.label);
            return false;
        }
        parsed << partition;
    }
    if (!foundEnd && bytes.size() % entrySize != 0) {
        error = "Partition table data ends with an incomplete entry.";
        return false;
    }
    if (parsed.isEmpty()) {
        error = "No partition entries were found in the standard table location.";
        return false;
    }
    partitions = parsed;
    return true;
}

QString partitionTypeName(quint8 type, quint8 subtype) {
    if (type == 0x00) {
        switch (subtype) {
        case 0x00: return "app/factory";
        case 0x10: return "app/ota-0";
        case 0x20: return "app/ota-1";
        default: return QString("app/0x%1").arg(subtype, 2, 16, QLatin1Char('0'));
        }
    }
    if (type == 0x01) {
        switch (subtype) {
        case 0x00: return "data/ota";
        case 0x01: return "data/phy";
        case 0x02: return "data/nvs";
        case 0x03: return "data/coredump";
        case 0x81: return "data/efuse";
        case 0x82: return "data/undefined";
        case 0x83: return "data/fat";
        case 0x84: return "data/spiffs";
        default: return QString("data/0x%1").arg(subtype, 2, 16, QLatin1Char('0'));
        }
    }
    return QString("type 0x%1/subtype 0x%2")
        .arg(type, 2, 16, QLatin1Char('0')).arg(subtype, 2, 16, QLatin1Char('0'));
}

}
