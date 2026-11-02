#include "esptool.h"
#include <QTest>
#include <algorithm>
#include <QCryptographicHash>
#include <QtEndian>

class EsptoolTests : public QObject {
    Q_OBJECT
private slots:
    void parsesAndLimitsSupportedVersions() {
        Esptool::Version version;
        QString error;
        QVERIFY(Esptool::parseVersion("esptool.py v4.8.1", version, error));
        QCOMPARE(version.major, 4);
        QVERIFY(Esptool::parseVersion("esptool v5.1.2", version, error));
        QCOMPARE(version.major, 5);
        QVERIFY(!Esptool::parseVersion("some unrelated program", version, error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!Esptool::parseVersion("esptool v6.0.0", version, error));
    }

    void argumentsKeepUserValuesAsSeparateArguments() {
        Esptool::Version version;
        version.major = 5;
        const auto args = Esptool::arguments(version, "COM port 7", 115200,
            Esptool::Command::ReadFlash, {"0x8000", "0xC00", "C:/dump files/table.bin"});
        QCOMPARE(args, QStringList({"--port", "COM port 7", "--baud", "115200",
            "read-flash", "0x8000", "0xC00", "C:/dump files/table.bin"}));
        version.major = 4;
        QCOMPARE(Esptool::arguments(version, "/dev/tty USB", 9600,
            Esptool::Command::ChipId), QStringList({"--port", "/dev/tty USB",
                "--baud", "9600", "chip_id"}));
    }

    void parsesEspIdfPartitionTable() {
        QByteArray bytes(96, char(0xff));
        auto *entry = reinterpret_cast<quint8 *>(bytes.data());
        entry[0] = 0xaa; entry[1] = 0x50; entry[2] = 1; entry[3] = 3;
        entry[4] = 0x00; entry[5] = 0x00; entry[6] = 0x01; entry[7] = 0x00;
        entry[8] = 0x00; entry[9] = 0x00; entry[10] = 0x04; entry[11] = 0x00;
        const QByteArray label("coredump");
        std::copy(label.cbegin(), label.cend(), bytes.begin() + 12);

        QVector<Esptool::Partition> partitions;
        QString error;
        QVERIFY2(Esptool::parsePartitionTable(bytes, partitions, error), qPrintable(error));
        QCOMPARE(partitions.size(), 1);
        QCOMPARE(partitions.first().offset, quint32(0x10000));
        QCOMPARE(partitions.first().size, quint32(0x40000));
        QCOMPARE(partitions.first().label, QString("coredump"));
        QCOMPARE(Esptool::partitionTypeName(1, 3), QString("data/coredump"));
    }

    void rejectsMalformedPartitionTable() {
        QVector<Esptool::Partition> partitions;
        QString error;
        QVERIFY(!Esptool::parsePartitionTable(QByteArray(32, '\0'), partitions, error));
        QVERIFY(error.contains("magic"));
        QVERIFY(!Esptool::parsePartitionTable(QByteArray(17, char(0xff)), partitions, error));
        QVERIFY(error.contains("incomplete"));
    }
    void correctOtaSubtypeNames() {
        QCOMPARE(Esptool::partitionTypeName(0, 0x10), QString("app/ota-0"));
        QCOMPARE(Esptool::partitionTypeName(0, 0x11), QString("app/ota-1"));
        QCOMPARE(Esptool::partitionTypeName(0, 0x1f), QString("app/ota-15"));
        QCOMPARE(Esptool::partitionTypeName(0, 0x20), QString("app/test"));
        QCOMPARE(Esptool::partitionTypeName(1, 0x82), QString("data/spiffs"));
    }

    void partitionTableChecksumAndOverlapValidation() {
        QByteArray bytes(96, char(0xff));
        auto *entry = reinterpret_cast<uchar *>(bytes.data());
        std::fill(entry, entry + 32, 0);
        entry[0] = 0xaa; entry[1] = 0x50; entry[3] = 0x10;
        qToLittleEndian<quint32>(0x10000, entry + 4);
        qToLittleEndian<quint32>(0x10000, entry + 8);
        bytes[32] = char(0xeb); bytes[33] = char(0xeb);
        bytes.replace(48, 16, QCryptographicHash::hash(bytes.left(32), QCryptographicHash::Md5));
        QVector<Esptool::Partition> partitions;
        QString error;
        QVERIFY2(Esptool::parsePartitionTable(bytes, partitions, error), qPrintable(error));
        bytes[48] = char(bytes[48] ^ 1);
        QVERIFY(!Esptool::parsePartitionTable(bytes, partitions, error));
        QVERIFY(error.contains("MD5"));
        bytes.replace(32, 32, bytes.left(32));
        QVERIFY(!Esptool::parsePartitionTable(bytes, partitions, error));
        QVERIFY(error.contains("Overlapping"));
    }

    void otaMetadataUsesBothCrcCheckedSectorsAndNeverClaimsRunning() {
        const QVector<Esptool::Partition> partitions{
            {0, 0x10, 0x10000, 0x10000, "ota0", 0},
            {0, 0x11, 0x20000, 0x10000, "ota1", 0}};
        QByteArray bytes(8192, char(0xff));
        auto *first = reinterpret_cast<uchar *>(bytes.data());
        auto *second = first + 4096;
        qToLittleEndian<quint32>(1, first);
        qToLittleEndian<quint32>(2, first + 24);
        qToLittleEndian<quint32>(0x4743989a, first + 28);
        qToLittleEndian<quint32>(2, second);
        qToLittleEndian<quint32>(2, second + 24);
        qToLittleEndian<quint32>(0x55f63774, second + 28);
        QString error;
        Esptool::OtaSelection selection;
        QVERIFY(Esptool::parseOtaData(bytes, partitions, selection, error));
        QCOMPARE(selection.selectedSlot, 1);
        QVERIFY(selection.entries[0].crcValid);
        QVERIFY(selection.entries[1].crcValid);
        QVERIFY(selection.explanation.contains("NOT proof"));
        QVERIFY(selection.explanation.contains("No partition is automatically"));
        qToLittleEndian<quint32>(1, second + 24);
        QVERIFY(Esptool::parseOtaData(bytes, partitions, selection, error));
        QVERIFY(selection.explanation.contains("Rollback-sensitive"));
        qToLittleEndian<quint32>(4, second + 24);
        QVERIFY(Esptool::parseOtaData(bytes, partitions, selection, error));
        QCOMPARE(selection.selectedSlot, 0);
        qToLittleEndian<quint32>(0, first + 28);
        QVERIFY(Esptool::parseOtaData(bytes, partitions, selection, error));
        QCOMPARE(selection.selectedSlot, -1);
        QVERIFY(selection.explanation.contains("No usable"));
        QVERIFY(!Esptool::parseOtaData(bytes.left(4096), partitions, selection, error));
        const QVector<Esptool::Partition> gap{{0, 0x11, 0x10000, 0x10000, "", 0}};
        QVERIFY(!Esptool::parseOtaData(bytes, gap, selection, error));
    }

    void otaStatesAndRedundancy_data() {
        QTest::addColumn<quint32>("sequence");
        QTest::addColumn<quint32>("state");
        QTest::addColumn<quint32>("crc");
        QTest::addColumn<int>("slot");
        QTest::addColumn<QString>("explanation");
        QTest::newRow("erased") << quint32(0xffffffff) << quint32(0xffffffff)
            << quint32(0xffffffff) << -1 << QString("No usable");
        QTest::newRow("new-sequence-wrap") << quint32(3) << quint32(0)
            << quint32(0xed4a5011) << 0 << QString("Rollback-sensitive");
        QTest::newRow("undefined-with-valid-crc") << quint32(2) << quint32(0xffffffff)
            << quint32(0x55f63774) << 1 << QString("UNDEFINED");
        QTest::newRow("invalid") << quint32(2) << quint32(3)
            << quint32(0x55f63774) << 0 << QString("sequence 1");
        QTest::newRow("unknown") << quint32(2) << quint32(5)
            << quint32(0x55f63774) << -1 << QString("Unknown OTA state");
        QTest::newRow("tie-prefers-copy-zero") << quint32(1) << quint32(1)
            << quint32(0x4743989a) << 0 << QString("sequence 1, VALID");
    }

    void otaStatesAndRedundancy() {
        QFETCH(quint32, sequence);
        QFETCH(quint32, state);
        QFETCH(quint32, crc);
        QFETCH(int, slot);
        QFETCH(QString, explanation);
        const QVector<Esptool::Partition> partitions{
            {0, 0x10, 0x10000, 0x10000, "", 0},
            {0, 0x11, 0x20000, 0x10000, "", 0}};
        QByteArray bytes(8192, char(0xff));
        auto *entry = reinterpret_cast<uchar *>(bytes.data());
        if (sequence != 0xffffffff) {
            qToLittleEndian<quint32>(1, entry);
            qToLittleEndian<quint32>(2, entry + 24);
            qToLittleEndian<quint32>(0x4743989a, entry + 28);
        }
        entry += 4096;
        qToLittleEndian(sequence, entry);
        qToLittleEndian(state, entry + 24);
        qToLittleEndian(crc, entry + 28);
        Esptool::OtaSelection selection;
        QString error;
        QVERIFY(Esptool::parseOtaData(bytes, partitions, selection, error));
        QCOMPARE(selection.selectedSlot, slot);
        QVERIFY2(selection.explanation.contains(explanation), qPrintable(selection.explanation));
        auto duplicate = partitions;
        duplicate << partitions.first();
        QVERIFY(!Esptool::parseOtaData(bytes, duplicate, selection, error));
        QVERIFY(error.contains("Duplicate"));
    }
};

QTEST_APPLESS_MAIN(EsptoolTests)
#include "test_esptool.moc"
