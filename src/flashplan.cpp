#include "flashplan.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QRegularExpression>
#include <QtEndian>
#include <algorithm>

namespace FlashPlan {
namespace {

const QMap<QString, quint16> chipIds = {
    {"esp32", 0}, {"esp32s2", 2}, {"esp32c3", 5}, {"esp32s3", 9},
    {"esp32c2", 12}, {"esp32c6", 13}, {"esp32h2", 16}, {"esp32p4", 18},
    {"esp32c61", 20}, {"esp32c5", 23}, {"esp32h21", 25}, {"esp32h4", 28},
    {"esp32s31", 32}
};

bool fingerprint(const QString &path, quint64 &size, QByteArray &sha256, QString &error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = path + ": " + file.errorString();
        return false;
    }
    if (!QFileInfo(file).isFile() || file.size() <= 0 || file.size() > 1073741824) {
        error = path + ": expected a nonempty regular file no larger than 1 GiB.";
        return false;
    }
    size = quint64(file.size());
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file) || file.error() != QFileDevice::NoError) {
        error = path + ": could not hash the complete file.";
        return false;
    }
    sha256 = hash.result();
    return true;
}

bool inspectImage(const Input &input, const QString &chip, QString &details, QString &error) {
    QFile file(input.path);
    if (!file.open(QIODevice::ReadOnly) || input.imageOffset > quint64(file.size())
        || !file.seek(qint64(input.imageOffset))) {
        error = "Cannot read the explicitly selected image header: " + input.path;
        return false;
    }
    QCryptographicHash digest(QCryptographicHash::Sha256);
    const QByteArray header = file.read(24);
    if (header.size() != 24 || quint8(header[0]) != 0xe9
        || quint8(header[1]) < 1 || quint8(header[1]) > 16 || quint8(header[23]) > 1) {
        error = "Invalid ESP image header (not an app/bootloader image, or wrong merged header offset).";
        return false;
    }
    const quint16 id = qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar *>(header.constData() + 12));
    if (id != chipIds.value(chip, 0xffff)) {
        error = QString("ESP image chip ID %1 does not match the explicitly chosen target %2.")
            .arg(id).arg(chip);
        return false;
    }
    digest.addData(header);
    quint8 checksum = 0xef;
    for (int segment = 0; segment < quint8(header[1]); ++segment) {
        const QByteArray segmentHeader = file.read(8);
        if (segmentHeader.size() != 8) {
            error = "Truncated ESP image segment header.";
            return false;
        }
        digest.addData(segmentHeader);
        quint32 remaining = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar *>(segmentHeader.constData() + 4));
        if (remaining > quint64(file.size() - file.pos())) {
            error = "ESP image segment exceeds the file size.";
            return false;
        }
        while (remaining) {
            const QByteArray chunk = file.read(qMin<quint32>(remaining, 65536));
            if (chunk.isEmpty()) {
                error = "Could not read the complete ESP image segment.";
                return false;
            }
            for (char byte : chunk) checksum ^= quint8(byte);
            digest.addData(chunk);
            remaining -= quint32(chunk.size());
        }
    }
    const quint64 relativeEnd = quint64(file.pos()) - input.imageOffset;
    const int paddingAndChecksum = int(16 - relativeEnd % 16);
    const QByteArray tail = file.read(paddingAndChecksum);
    if (tail.size() != paddingAndChecksum || quint8(tail.back()) != checksum) {
        error = "ESP image checksum is invalid or truncated.";
        return false;
    }
    digest.addData(tail);
    const bool hasHash = header[23] == 1;
    if (hasHash && file.read(32) != digest.result()) {
        error = "ESP image appended SHA-256 is invalid or truncated.";
        return false;
    }
    const auto *revision = reinterpret_cast<const uchar *>(header.constData() + 15);
    details = QString("ESP image: %1; revision bounds %2..%3; XOR checksum checked; %4. "
                      "Not a signature or device compatibility check.")
        .arg(chip).arg(qFromLittleEndian<quint16>(revision))
        .arg(qFromLittleEndian<quint16>(revision + 2))
        .arg(hasHash ? "appended SHA-256 checked" : "no appended SHA-256");
    return true;
}

}

QStringList supportedChips() { return chipIds.keys(); }

bool parseNumber(const QString &text, quint64 &value) {
    const QString input = text.trimmed();
    if (!QRegularExpression("^(?:0[xX][0-9a-fA-F]+|[0-9]+)$").match(input).hasMatch()) return false;
    bool ok = false;
    const bool hex = input.startsWith("0x", Qt::CaseInsensitive);
    const auto number = (hex ? input.mid(2) : input).toULongLong(&ok, hex ? 16 : 10);
    if (ok) value = number;
    return ok;
}

QString kindName(Kind kind) {
    switch (kind) {
    case Kind::App: return "App";
    case Kind::Bootloader: return "Bootloader";
    case Kind::PartitionTable: return "Partition table";
    case Kind::Data: return "Data (opaque)";
    case Kind::Merged: return "Merged image";
    }
    return {};
}

bool loadManifest(const QString &path, const QString &chip, QVector<Input> &inputs,
                  QString &error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4194304) {
        error = "Cannot read flasher_args.json (maximum 4 MiB): " + file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        error = "Invalid flasher_args.json: " + parseError.errorString();
        return false;
    }
    const auto root = json.object();
    const auto args = root.value("extra_esptool_args").toObject();
    if (!chipIds.contains(chip) || args.value("chip").toString() != chip) {
        error = "Manifest chip is missing, unsupported or differs from the chosen target.";
        return false;
    }
    for (const auto &key : {"encrypt", "encrypt_files", "encrypted_files"}) {
        if (root.contains(key)) {
            error = "Encrypted flash packages are not supported by this preview.";
            return false;
        }
    }
    const auto flashFiles = root.value("flash_files").toObject();
    if (flashFiles.isEmpty() || flashFiles.size() > 128) {
        error = "Manifest must contain 1..128 flash_files entries.";
        return false;
    }
    QMap<quint64, Kind> declaredKinds;
    for (const auto &role : {QString("app"), QString("bootloader"), QString("partition-table")}) {
        if (!root.contains(role)) continue;
        const auto object = root.value(role).toObject();
        quint64 address = 0;
        if (!parseNumber(object.value("offset").toString(), address)
            || !object.value("file").isString()
            || (object.contains("encrypted") && object.value("encrypted") != QJsonValue(false))) {
            error = "Invalid or encrypted manifest component: " + role;
            return false;
        }
        if (declaredKinds.contains(address)) {
            error = "Manifest component roles have duplicate addresses.";
            return false;
        }
        bool matches = false;
        for (auto it = flashFiles.begin(); it != flashFiles.end(); ++it) {
            quint64 candidate = 0;
            if (parseNumber(it.key(), candidate) && candidate == address
                && it.value().toString() == object.value("file").toString()) matches = true;
        }
        if (!matches) {
            error = "Manifest component disagrees with flash_files: " + role;
            return false;
        }
        declaredKinds[address] = role == "app" ? Kind::App
            : role == "bootloader" ? Kind::Bootloader : Kind::PartitionTable;
    }
    const auto writeArgs = root.value("write_flash_args");
    if (!writeArgs.isUndefined()) {
        if (!writeArgs.isArray()) {
            error = "write_flash_args must be an array.";
            return false;
        }
        const auto options = writeArgs.toArray();
        for (const auto &arg : options) {
            if (!arg.isString() || arg.toString().contains("encrypt")) {
                error = "Unsupported or encrypted write_flash_args.";
                return false;
            }
        }
    }
    QVector<Input> parsed;
    const QDir directory = QFileInfo(path).absoluteDir();
    for (auto it = flashFiles.begin(); it != flashFiles.end(); ++it) {
        quint64 address = 0;
        if (!parseNumber(it.key(), address) || !it.value().isString()
            || it.value().toString().isEmpty()) {
            error = "Invalid address or file path in flash_files.";
            return false;
        }
        parsed << Input{directory.absoluteFilePath(it.value().toString()), address,
                        declaredKinds.value(address, Kind::Data), 0};
    }
    inputs = parsed;
    return true;
}

bool inspect(const QVector<Input> &inputs, const QString &chip, quint64 flashSize,
             const QVector<Esptool::Partition> &partitions, Plan &plan, QString &error) {
    if (!chipIds.contains(chip) || flashSize == 0 || flashSize > 0x100000000ULL
        || flashSize % 4096 || inputs.isEmpty() || inputs.size() > 128) {
        error = "Choose a supported chip, sector-aligned flash size and 1..128 input files.";
        return false;
    }
    Plan parsed;
    parsed.chip = chip;
    parsed.flashSize = flashSize;
    QVector<Esptool::Partition> effectiveLayout = partitions;
    bool packageLayout = false;
    QByteArray packageTableDigest;
    for (const auto &input : inputs) {
        if (input.kind != Kind::PartitionTable) continue;
        if (packageLayout) {
            error = "Multiple partition tables in one plan are not supported.";
            return false;
        }
        QFile table(input.path);
        if (!table.open(QIODevice::ReadOnly) || table.size() > 4096) {
            error = "Cannot read package partition table (maximum one sector): " + input.path;
            return false;
        }
        const auto bytes = table.readAll();
        packageTableDigest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
        if (!Esptool::parsePartitionTable(bytes, effectiveLayout, error)) return false;
        packageLayout = true;
    }
    QStringList report;
    report << "OFFLINE PREVIEW ONLY - NO FLASH WRITES OR BOOT SELECTION CHANGES"
        << QString("Chosen target: %1; declared flash capacity: %2 bytes (not device-verified).")
            .arg(chip).arg(flashSize)
        << "The running partition and device security status are UNKNOWN. No inactive target is assumed.";
    report << (packageLayout ? "App-fit checks use the NEW package partition table, not the live layout."
                             : "App-fit checks use the supplied read snapshot, if any.");
    for (const auto &partition : effectiveLayout) {
        if (quint64(partition.offset) + partition.size > flashSize) {
            error = "A loaded partition extends beyond the declared flash capacity.";
            return false;
        }
    }
    for (const auto &input : inputs) {
        File entry;
        entry.input = input;
        if (!fingerprint(input.path, entry.size, entry.sha256, error)) return false;
        if (input.address % 4096 || input.address >= flashSize
            || entry.size > flashSize - input.address) {
            error = "Unaligned address or file outside declared flash: " + input.path;
            return false;
        }
        entry.eraseStart = input.address;
        entry.eraseEnd = ((input.address + entry.size + 4095) / 4096) * 4096;
        for (const auto &other : parsed.files) {
            if (entry.eraseStart < other.eraseEnd && other.eraseStart < entry.eraseEnd) {
                error = "Overlapping file or erase-sector ranges: " + input.path
                    + " and " + other.input.path;
                return false;
            }
        }
        if (input.kind == Kind::App || input.kind == Kind::Bootloader || input.kind == Kind::Merged) {
            if (input.kind != Kind::Merged && input.imageOffset != 0) {
                error = "Only merged images accept a nonzero image header offset.";
                return false;
            }
            if (!inspectImage(input, chip, entry.details, error)) return false;
        } else if (input.kind == Kind::PartitionTable) {
            if (entry.sha256 != packageTableDigest) {
                error = "Package partition table changed during inspection.";
                return false;
            }
            if (entry.size > 4096) {
                error = "Partition table input exceeds one sector.";
                return false;
            }
            for (const auto &partition : effectiveLayout) {
                if (quint64(partition.offset) + partition.size > flashSize) {
                    error = "New partition table exceeds declared flash capacity.";
                    return false;
                }
                if (partition.offset < entry.eraseEnd
                    && entry.eraseStart < quint64(partition.offset) + partition.size) {
                    error = "A partition overlaps the partition table's erase sector.";
                    return false;
                }
            }
            entry.details = "Partition table parsed. LAYOUT CHANGE: existing OTA/data contents may become invalid.";
        } else {
            entry.details = "Opaque data: semantics, encryption and target role are NOT verified.";
        }
        if (input.kind == Kind::App) {
            if (input.address % 65536) {
                error = "App address must be aligned to 64 KiB.";
                return false;
            }
            if (!effectiveLayout.isEmpty()) {
                bool fits = false;
                for (const auto &partition : effectiveLayout) {
                    if (partition.type == 0 && partition.offset == input.address
                        && entry.eraseEnd <= quint64(partition.offset) + partition.size) fits = true;
                }
                if (!fits) {
                    error = "App does not fit an app partition at the chosen address in the loaded layout.";
                    return false;
                }
                entry.details += " Fits chosen app partition; not established as inactive.";
            } else {
                entry.details += " No partition layout supplied: app partition fit is NOT verified.";
            }
        }
        if (input.kind == Kind::Merged) {
            entry.details += " Merged span includes padding and may replace bootloader, partition table, "
                "otadata and existing apps/data. Embedded layout and all other images are NOT verified.";
        }
        if (input.kind == Kind::Bootloader && !effectiveLayout.isEmpty()) {
            for (const auto &partition : effectiveLayout) {
                if (entry.eraseStart < quint64(partition.offset) + partition.size
                    && partition.offset < entry.eraseEnd) {
                    error = "Bootloader range overlaps an app/data partition.";
                    return false;
                }
            }
        }
        quint64 checkedSize = 0;
        QByteArray checkedDigest;
        if (!fingerprint(input.path, checkedSize, checkedDigest, error)) return false;
        if (checkedSize != entry.size || checkedDigest != entry.sha256) {
            error = "Input changed during inspection: " + input.path;
            return false;
        }
        report << "" << QString("%1: %2").arg(kindName(input.kind), QFileInfo(input.path).absoluteFilePath())
            << QString("Write span [0x%1, 0x%2), %3 bytes; erase sectors [0x%4, 0x%5).")
                .arg(input.address, 0, 16).arg(input.address + entry.size, 0, 16).arg(entry.size)
                .arg(entry.eraseStart, 0, 16).arg(entry.eraseEnd, 0, 16)
            << "SHA-256: " + QString::fromLatin1(entry.sha256.toHex()) << entry.details;
        parsed.files << entry;
    }
    if (!revalidate(parsed, error)) return false;
    report << "" << "WRITE BLOCKED: live chip/revision, flash capacity, security/anti-rollback, "
        "running app and bootloader compatibility have not been verified. "
        "This preview cannot flash, erase or modify otadata.";
    parsed.report = report.join('\n');
    plan = parsed;
    return true;
}

bool revalidate(const Plan &plan, QString &error) {
    if (plan.files.isEmpty()) {
        error = "No inspected files to revalidate.";
        return false;
    }
    for (const auto &entry : plan.files) {
        quint64 size = 0;
        QByteArray digest;
        if (!fingerprint(entry.input.path, size, digest, error)) return false;
        if (size != entry.size || digest != entry.sha256) {
            error = "File changed since preview: " + entry.input.path;
            return false;
        }
    }
    return true;
}

}
