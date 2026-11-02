#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace Esptool {

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    QString text;
};

enum class Command { ChipId, FlashId, ReadFlash };

struct Partition {
    quint8 type = 0;
    quint8 subtype = 0;
    quint32 offset = 0;
    quint32 size = 0;
    QString label;
    quint32 flags = 0;
};

bool parseVersion(const QString &output, Version &version, QString &error);
QStringList versionArguments();
QStringList arguments(const Version &version, const QString &port, int baud,
                      Command command, const QStringList &extra = {});
bool parsePartitionTable(const QByteArray &bytes, QVector<Partition> &partitions,
                         QString &error);
QString partitionTypeName(quint8 type, quint8 subtype);

}
