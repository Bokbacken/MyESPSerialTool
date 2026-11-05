#pragma once

#include "esptool.h"
#include <QByteArray>
#include <QString>
#include <QVector>

namespace FlashPlan {

enum class Kind { App, Bootloader, PartitionTable, Data, Merged };

struct Input {
    QString path;
    quint64 address = 0;
    Kind kind = Kind::Data;
    quint64 imageOffset = 0;
};

struct File {
    Input input;
    quint64 size = 0;
    quint64 eraseStart = 0;
    quint64 eraseEnd = 0;
    QByteArray sha256;
    QString details;
};

struct Plan {
    QString chip;
    quint64 flashSize = 0;
    QVector<File> files;
    QString report;
};

QStringList supportedChips();
bool parseNumber(const QString &text, quint64 &value);
QString kindName(Kind kind);
bool loadManifest(const QString &path, const QString &chip, QVector<Input> &inputs,
                  QString &error);
bool inspect(const QVector<Input> &inputs, const QString &chip, quint64 flashSize,
             const QVector<Esptool::Partition> &partitions, Plan &plan, QString &error);
bool revalidate(const Plan &plan, QString &error);

}
