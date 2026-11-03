#include "esptool.h"
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QSet>
#include <QtEndian>

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
        case Command::WriteFlash: name = "write-flash"; break;
        }
    } else {
        switch (command) {
        case Command::ChipId: name = "chip_id"; break;
        case Command::FlashId: name = "flash_id"; break;
        case Command::ReadFlash: name = "read_flash"; break;
        case Command::WriteFlash: name = "write_flash"; break;
        }
    }
    QStringList result{"--port", port, "--baud", QString::number(baud), name};
    result.append(extra);
    return result;
}

bool parseDetectedChip(const QString &output, QString &chip, QString &error) {
    const auto match = QRegularExpression(
        R"(\bChip is\s+([A-Za-z0-9-]+))", QRegularExpression::CaseInsensitiveOption)
        .match(output);
    if (!match.hasMatch()) {
        error = "Could not identify the connected chip from esptool output.";
        return false;
    }
    const QString normalized = match.captured(1).toLower().remove('-');
    static const QSet<QString> supported = {
        "esp32", "esp32s2", "esp32c3", "esp32s3", "esp32c2", "esp32c6",
        "esp32h2", "esp32p4", "esp32c61", "esp32c5", "esp32h21", "esp32h4", "esp32s31"
    };
    if (!supported.contains(normalized)) {
        error = "Connected chip is not supported for guarded flashing: " + match.captured(1);
        return false;
    }
    chip = normalized;
    return true;
}

bool parseFlashCapacity(const QString &output, quint64 &bytes, QString &error) {
    const auto match = QRegularExpression(
        R"(Detected flash size:\s*([0-9]+)\s*(bytes?|[KMG]B))",
        QRegularExpression::CaseInsensitiveOption).match(output);
    if (!match.hasMatch()) {
        error = "Could not identify the connected flash capacity from esptool output.";
        return false;
    }
    bool ok = false;
    quint64 size = match.captured(1).toULongLong(&ok);
    const QString unit = match.captured(2).toLower();
    if (!ok) {
        error = "Invalid detected flash capacity.";
        return false;
    }
    quint64 multiplier = 1;
    if (unit.startsWith('k')) multiplier = 1024;
    else if (unit.startsWith('m')) multiplier = 1024 * 1024;
    else if (unit.startsWith('g')) multiplier = 1024ULL * 1024ULL * 1024ULL;
    if (size > 0x100000000ULL / multiplier) {
        error = "Detected flash capacity exceeds the supported address space.";
        return false;
    }
    size *= multiplier;
    if (size == 0 || size > 0x100000000ULL || size % 4096) {
        error = "Detected flash capacity is invalid or not sector-aligned.";
        return false;
    }
    bytes = size;
    return true;
}

bool parsePartitionTable(const QByteArray &bytes, QVector<Partition> &partitions,
                         QString &error) {
    constexpr int entrySize = 32;
    QVector<Partition> parsed;
    bool foundEnd = false;
    bool foundDigest = false;
    for (int position = 0; position + entrySize <= bytes.size(); position += entrySize) {
        const auto *entry = reinterpret_cast<const quint8 *>(bytes.constData() + position);
        bool erased = true;
        for (int i = 0; i < entrySize; ++i) erased = erased && entry[i] == 0xff;
        if (erased) {
            foundEnd = true;
            break;
        }
        if (entry[0] == 0xeb && entry[1] == 0xeb) {
            if (foundDigest || bytes.mid(position + 2, 14) != QByteArray(14, char(0xff))
                || bytes.mid(position + 16, 16)
                    != QCryptographicHash::hash(bytes.left(position), QCryptographicHash::Md5)) {
                error = "Partition table MD5 checksum is invalid.";
                return false;
            }
            foundDigest = true;
            continue;
        }
        if (foundDigest) {
            error = "Unexpected partition entry after the partition table checksum.";
            return false;
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
        if (partition.offset % 4096 || partition.size % 4096
            || (partition.type == 0 && partition.offset % 65536)) {
            error = QString("Invalid alignment in partition '%1'.").arg(partition.label);
            return false;
        }
        for (const auto &other : parsed) {
            if (quint64(partition.offset) < quint64(other.offset) + other.size
                && quint64(other.offset) < quint64(partition.offset) + partition.size) {
                error = QString("Overlapping partitions '%1' and '%2'.")
                    .arg(other.label, partition.label);
                return false;
            }
        }
        parsed << partition;
    }
    if (!foundEnd) {
        error = bytes.size() % entrySize != 0
            ? "Partition table data ends with an incomplete entry."
            : "Partition table is missing an end marker.";
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
        if (subtype >= 0x10 && subtype <= 0x1f)
            return QString("app/ota-%1").arg(subtype - 0x10);
        switch (subtype) {
        case 0x00: return "app/factory";
        case 0x20: return "app/test";
        default: return QString("app/0x%1").arg(subtype, 2, 16, QLatin1Char('0'));
        }
    }
    if (type == 0x01) {
        switch (subtype) {
        case 0x00: return "data/ota";
        case 0x01: return "data/phy";
        case 0x02: return "data/nvs";
        case 0x03: return "data/coredump";
        case 0x05: return "data/efuse";
        case 0x81: return "data/fat";
        case 0x82: return "data/spiffs";
        case 0x83: return "data/littlefs";
        default: return QString("data/0x%1").arg(subtype, 2, 16, QLatin1Char('0'));
        }
    }
    return QString("type 0x%1/subtype 0x%2")
        .arg(type, 2, 16, QLatin1Char('0')).arg(subtype, 2, 16, QLatin1Char('0'));
}

QString otaStateName(quint32 state) {
    switch (state) {
    case 0: return "NEW";
    case 1: return "PENDING_VERIFY";
    case 2: return "VALID";
    case 3: return "INVALID";
    case 4: return "ABORTED";
    case 0xffffffff: return "UNDEFINED";
    default: return QString("UNKNOWN (0x%1)").arg(state, 0, 16);
    }
}

bool parseOtaData(const QByteArray &bytes, const QVector<Partition> &partitions,
                  OtaSelection &selection, QString &error) {
    if (bytes.size() != 8192) {
        error = "ESP-IDF otadata must contain both complete 4096-byte sectors (8192 bytes).";
        return false;
    }
    QSet<int> otaSlots;
    for (const auto &partition : partitions) {
        if (partition.type != 0 || partition.subtype < 0x10 || partition.subtype > 0x1f) continue;
        const int slot = partition.subtype - 0x10;
        if (otaSlots.contains(slot)) {
            error = "Duplicate OTA slot subtype; cannot interpret OTA selection safely.";
            return false;
        }
        otaSlots.insert(slot);
    }
    if (otaSlots.isEmpty()) {
        error = "No OTA app slots in the partition table.";
        return false;
    }
    for (int slot = 0; slot < otaSlots.size(); ++slot) {
        if (!otaSlots.contains(slot)) {
            error = "OTA slot subtypes are not contiguous from OTA0; selection is ambiguous.";
            return false;
        }
    }
    OtaSelection parsed;
    int selected = -1;
    bool unknownState = false;
    for (int copy = 0; copy < 2; ++copy) {
        const auto *entry = reinterpret_cast<const uchar *>(bytes.constData() + copy * 4096);
        OtaEntry record;
        record.sequence = qFromLittleEndian<quint32>(entry);
        record.state = qFromLittleEndian<quint32>(entry + 24);
        // ESP-IDF uses esp_rom_crc32_le(UINT32_MAX, &ota_seq, 4):
        // ROM complements on entry and exit, so the bitwise seed here is zero.
        quint32 crc = 0;
        for (int i = 0; i < 4; ++i) {
            crc ^= entry[i];
            for (int bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0U);
        }
        record.crcValid = ~crc == qFromLittleEndian<quint32>(entry + 28);
        const bool knownState = record.state <= 4 || record.state == 0xffffffff;
        unknownState = unknownState || (!knownState && record.crcValid);
        record.valid = record.crcValid && record.sequence != 0xffffffff
            && record.sequence != 0 && knownState && record.state != 3 && record.state != 4;
        parsed.entries << record;
        if (record.valid && (selected < 0
            || record.sequence > parsed.entries[selected].sequence)) selected = copy;
    }
    if (unknownState) {
        parsed.explanation = "Unknown OTA state: boot selection is not inferred.";
    } else if (selected < 0) {
        parsed.explanation = "No usable OTA selection. Factory/default/fallback boot is not inferred.";
    } else {
        const auto &record = parsed.entries[selected];
        parsed.selectedSlot = int((record.sequence - 1) % quint32(otaSlots.size()));
        parsed.explanation = QString("ESP-IDF metadata candidate: OTA%1 (sequence %2, %3).")
            .arg(parsed.selectedSlot).arg(record.sequence).arg(otaStateName(record.state));
        if (record.state == 0 || record.state == 1)
            parsed.explanation += " Rollback-sensitive state: the next boot may change this selection.";
    }
    parsed.explanation += " This is NOT proof of the running or last-running app. "
        "Rollback, anti-rollback and image validation can select another partition. "
        "No partition is automatically designated safe to overwrite.";
    selection = parsed;
    return true;
}

}
