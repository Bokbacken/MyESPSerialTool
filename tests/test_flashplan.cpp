#include "flashplan.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
#include <algorithm>

namespace {

bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray image(quint16 chipId = 9) {
    QByteArray bytes(24, '\0');
    bytes[0] = char(0xe9);
    bytes[1] = 1;
    bytes[23] = 1;
    qToLittleEndian(chipId, reinterpret_cast<uchar *>(bytes.data() + 12));
    QByteArray segmentHeader(8, '\0');
    qToLittleEndian<quint32>(4, reinterpret_cast<uchar *>(segmentHeader.data() + 4));
    bytes += segmentHeader;
    bytes += QByteArray::fromHex("01020304");
    quint8 checksum = 0xef ^ 1 ^ 2 ^ 3 ^ 4;
    while ((bytes.size() + 1) % 16) bytes += '\0';
    bytes += char(checksum);
    bytes += QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    return bytes;
}

QVector<Esptool::Partition> layout() {
    return {{0, 0x10, 0x10000, 0x10000, "ota_0", 0},
            {0, 0x11, 0x20000, 0x10000, "ota_1", 0}};
}

QByteArray table() {
    QByteArray bytes(96, char(0xff));
    for (int i = 0; i < 2; ++i) {
        char *entry = bytes.data() + i * 32;
        std::fill(entry, entry + 32, '\0');
        entry[0] = char(0xaa);
        entry[1] = char(0x50);
        entry[3] = char(0x10 + i);
        qToLittleEndian<quint32>(quint32(0x10000 + i * 0x10000),
            reinterpret_cast<uchar *>(entry + 4));
        qToLittleEndian<quint32>(0x10000, reinterpret_cast<uchar *>(entry + 8));
    }
    return bytes;
}

}

class FlashPlanTests : public QObject {
    Q_OBJECT
private slots:
    void explicitAppChecksChipFitChecksumAndDigest() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("arbitrary name.bin");
        QVERIFY(writeFile(path, image()));
        FlashPlan::Plan plan;
        QString error;
        const QVector<FlashPlan::Input> inputs{{path, 0x20000, FlashPlan::Kind::App, 0}};
        QVERIFY2(FlashPlan::inspect(inputs, "esp32s3", 0x400000, layout(), plan, error), qPrintable(error));
        QCOMPARE(plan.files.first().eraseStart, quint64(0x20000));
        QCOMPARE(plan.files.first().eraseEnd, quint64(0x21000));
        QVERIFY(plan.report.contains("NOT detected") || plan.report.contains("not device-verified"));
        QVERIFY(plan.report.contains("WRITE BLOCKED"));
        QVERIFY(plan.report.contains("not established as inactive"));
        QVERIFY(FlashPlan::revalidate(plan, error));
        QVERIFY(!FlashPlan::inspect(inputs, "esp32c3", 0x400000, layout(), plan, error));
        QVERIFY(error.contains("chip ID"));
        auto corrupted = image();
        corrupted[32] = char(0x40);
        QVERIFY(writeFile(path, corrupted));
        QVERIFY(!FlashPlan::revalidate(plan, error));
        QVERIFY(error.contains("changed"));
        QVERIFY(!FlashPlan::inspect(inputs, "esp32s3", 0x400000, layout(), plan, error));
        QVERIFY(error.contains("checksum"));
        corrupted = image();
        corrupted[corrupted.size() - 1] = char(corrupted.back() ^ 1);
        QVERIFY(writeFile(path, corrupted));
        QVERIFY(!FlashPlan::inspect(inputs, "esp32s3", 0x400000, layout(), plan, error));
        QVERIFY(error.contains("SHA-256"));
    }

    void rejectsWrongPartitionAndTruncatedApp() {
        QTemporaryDir directory;
        const QString path = directory.filePath("app.bin");
        QVERIFY(writeFile(path, image()));
        FlashPlan::Plan plan;
        QString error;
        QVERIFY(!FlashPlan::inspect({{path, 0x30000, FlashPlan::Kind::App, 0}},
            "esp32s3", 0x400000, layout(), plan, error));
        QVERIFY(error.contains("does not fit"));
        QVERIFY(writeFile(path, image().left(35)));
        QVERIFY(!FlashPlan::inspect({{path, 0x20000, FlashPlan::Kind::App, 0}},
            "esp32s3", 0x400000, layout(), plan, error));
        QVERIFY(error.contains("segment"));
        auto bytes = image();
        bytes += QByteArray(0x10001 - bytes.size(), '\0');
        QVERIFY(writeFile(path, bytes));
        QVERIFY(!FlashPlan::inspect({{path, 0x20000, FlashPlan::Kind::App, 0}},
            "esp32s3", 0x400000, layout(), plan, error));
        QVERIFY(error.contains("does not fit"));
    }

    void rejectsMissingEmptyUnalignedOutOfBoundsAndOverlappingFiles() {
        QTemporaryDir directory;
        const QString path = directory.filePath("data.bin");
        FlashPlan::Plan plan;
        QString error;
        QVERIFY(!FlashPlan::inspect({{path, 0, FlashPlan::Kind::Data, 0}},
            "esp32", 0x400000, {}, plan, error));
        QVERIFY(writeFile(path, {}));
        QVERIFY(!FlashPlan::inspect({{path, 0, FlashPlan::Kind::Data, 0}},
            "esp32", 0x400000, {}, plan, error));
        QVERIFY(writeFile(path, QByteArray(4097, 'x')));
        QVERIFY(!FlashPlan::inspect({{path, 1, FlashPlan::Kind::Data, 0}},
            "esp32", 0x400000, {}, plan, error));
        QVERIFY(!FlashPlan::inspect({{path, 0x3ff000, FlashPlan::Kind::Data, 0}},
            "esp32", 0x400000, {}, plan, error));
        QVERIFY(!FlashPlan::inspect({{path, 0, FlashPlan::Kind::Data, 0},
                                    {path, 0x1000, FlashPlan::Kind::Data, 0}},
            "esp32", 0x400000, {}, plan, error));
        QVERIFY(error.contains("Overlapping"));
    }

    void mergedUsesOnlyExplicitHeaderOffsetAndWarnsAboutLayout() {
        QTemporaryDir directory;
        const QString path = directory.filePath("merged.bin");
        QVERIFY(writeFile(path, QByteArray(4096, char(0xff)) + image()));
        FlashPlan::Plan plan;
        QString error;
        QVERIFY(!FlashPlan::inspect({{path, 0, FlashPlan::Kind::Merged, 0}},
            "esp32s3", 0x400000, {}, plan, error));
        QVERIFY(FlashPlan::inspect({{path, 0, FlashPlan::Kind::Merged, 4096}},
            "esp32s3", 0x400000, {}, plan, error));
        QVERIFY(plan.report.contains("otadata"));
        QVERIFY(plan.report.contains("Embedded layout and all other images are NOT verified"));
    }

    void manifestUsesDeclaredRolesNeverNamesAndRejectsBadMetadata() {
        QTemporaryDir directory;
        const QString app = directory.filePath("not-called-app.bin");
        const QString manifest = directory.filePath("flasher_args.json");
        const QString partitions = directory.filePath("not-called-partitions.bin");
        QVERIFY(writeFile(app, image()));
        QVERIFY(writeFile(partitions, table()));
        QJsonObject root{
            {"flash_files", QJsonObject{{"0x20000", "not-called-app.bin"},
                                       {"0x8000", "not-called-partitions.bin"}}},
            {"extra_esptool_args", QJsonObject{{"chip", "esp32s3"}}},
            {"app", QJsonObject{{"offset", "0x20000"}, {"file", "not-called-app.bin"}, {"encrypted", false}}},
            {"partition-table", QJsonObject{{"offset", "0x8000"}, {"file", "not-called-partitions.bin"},
                                             {"encrypted", false}}}
        };
        auto saveManifest = [&] { return writeFile(manifest, QJsonDocument(root).toJson()); };
        QVERIFY(saveManifest());
        QVector<FlashPlan::Input> inputs;
        QString error;
        QVERIFY2(FlashPlan::loadManifest(manifest, "esp32s3", inputs, error), qPrintable(error));
        QCOMPARE(inputs.size(), 2);
        FlashPlan::Plan plan;
        QVERIFY2(FlashPlan::inspect(inputs, "esp32s3", 0x400000, {}, plan, error), qPrintable(error));
        QVERIFY(plan.report.contains("NEW package partition table"));
        QVERIFY(!FlashPlan::loadManifest(manifest, "esp32", inputs, error));
        root.remove("app");
        QVERIFY(saveManifest());
        QVERIFY(FlashPlan::loadManifest(manifest, "esp32s3", inputs, error));
        bool opaque = false;
        for (const auto &input : inputs)
            if (input.address == 0x20000) opaque = input.kind == FlashPlan::Kind::Data;
        QVERIFY(opaque);
        root["encrypt_files"] = QJsonObject{};
        QVERIFY(saveManifest());
        QVERIFY(!FlashPlan::loadManifest(manifest, "esp32s3", inputs, error));
        QVERIFY(error.contains("Encrypted"));
        QVERIFY(writeFile(manifest, "{ invalid"));
        QVERIFY(!FlashPlan::loadManifest(manifest, "esp32s3", inputs, error));
    }

    void strictNumericInput() {
        quint64 value = 0;
        QVERIFY(FlashPlan::parseNumber("0x20000", value));
        QCOMPARE(value, quint64(0x20000));
        QVERIFY(FlashPlan::parseNumber("010", value));
        QCOMPARE(value, quint64(10));
        for (const auto &text : {"-1", "+1", "4MB", "0x", "18446744073709551616"})
            QVERIFY(!FlashPlan::parseNumber(text, value));
    }

    void exactFitAndPackageLayoutOverrideSnapshot() {
        QTemporaryDir directory;
        const QString app = directory.filePath("app.bin");
        const QString partitions = directory.filePath("table.bin");
        QByteArray bytes = image();
        bytes += QByteArray(65536 - bytes.size(), char(0xff));
        QVERIFY(writeFile(app, bytes));
        QVERIFY(writeFile(partitions, table()));
        FlashPlan::Plan plan;
        QString error;
        const QVector<FlashPlan::Input> inputs{
            {app, 0x20000, FlashPlan::Kind::App, 0},
            {partitions, 0x8000, FlashPlan::Kind::PartitionTable, 0}};
        const QVector<Esptool::Partition> oldLayout{{0, 0x10, 0x10000, 0x10000, "old", 0}};
        QVERIFY2(FlashPlan::inspect(inputs, "esp32s3", 0x30000, oldLayout, plan, error), qPrintable(error));
        QCOMPARE(plan.files.first().eraseEnd, quint64(0x30000));
        bytes += char(0xff);
        QVERIFY(writeFile(app, bytes));
        QVERIFY(!FlashPlan::inspect(inputs, "esp32s3", 0x400000, oldLayout, plan, error));
        QVERIFY(error.contains("does not fit"));
        QVERIFY(writeFile(app, image()));
        QVERIFY(!FlashPlan::inspect(inputs, "esp32s3", 0x2f000, {}, plan, error));
        QVERIFY(error.contains("partition extends"));
        QVERIFY(!FlashPlan::inspect({{app, 0x10000, FlashPlan::Kind::Bootloader, 0}},
            "esp32s3", 0x400000, layout(), plan, error));
        QVERIFY(error.contains("Bootloader range overlaps"));
    }
};

QTEST_APPLESS_MAIN(FlashPlanTests)
#include "test_flashplan.moc"
