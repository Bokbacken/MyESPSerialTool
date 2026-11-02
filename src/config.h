#pragma once
#include <QMap>
#include <QMetaType>
#include <QStringList>

struct Profile {
    QString name = QStringLiteral("ESP");
    QString port, usbSerial;
    int baud = 115200, dataBits = 8, vid = -1, pid = -1;
    QString parity = QStringLiteral("none"), stopBits = QStringLiteral("1");
    QString flowControl = QStringLiteral("none"), newline = QStringLiteral("lf");
    bool readOnly = true, dtr = false, rts = false, localEcho = false, follow = true;
    bool autoConnect = false;
    bool autoReconnect = true, newLogOnReconnect = false;
    int writeTimeoutMs = 2000, retryLimit = 5, terminalRows = 2000, lineColumns = 4096;
    bool logging = false, timestampMetadata = false, textLog = false;
    QString logDirectory = QStringLiteral("logs");
    QString logName = QStringLiteral("{profile}-{date}-{time}-{id}");
    int logSeconds = 0, displayQueueBytes = 262144;
    qint64 logBytes = 10485760, logQueueBytes = 4194304;
    int logRetention = 5;
    QMap<QString, QString> values() const;
    static bool fromValues(const QString &name, const QMap<QString, QString> &values,
                           Profile &out, QString &error);
};
Q_DECLARE_METATYPE(Profile)

class IniFile {
public:
    QString path;
    QList<Profile> profiles;
    bool load(const QString &file, QString &error);
    bool save(QString &error);
private:
    QStringList lines;
    bool utf8Bom = false, crlf = false;
};
