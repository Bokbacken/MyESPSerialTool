#include "esptool.h"
#include <QTest>
#include <algorithm>

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
};

QTEST_APPLESS_MAIN(EsptoolTests)
#include "test_esptool.moc"
