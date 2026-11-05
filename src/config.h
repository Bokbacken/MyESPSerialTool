#pragma once
#include <QMap>
#include <QMetaType>
#include <QStringList>

struct Profile {
    QString name = QStringLiteral("ESP");
    QString port, usbSerial;
    QString esptoolPath, esptoolChip;
    int esptoolBaud = 0;
    int baud = 115200, dataBits = 8, vid = -1, pid = -1;
    QString parity = QStringLiteral("none"), stopBits = QStringLiteral("1");
    QString flowControl = QStringLiteral("none"), newline = QStringLiteral("lf");
    bool readOnly = true, dtr = false, rts = false, localEcho = false, follow = true;
    bool autoConnect = false;
    bool autoReconnect = true, newLogOnReconnect = false;
    QString filterText, filterMode = QStringLiteral("text");
    bool filterCase = false, filterHighlight = false;
    int writeTimeoutMs = 2000, retryLimit = 5, terminalRows = 2000, lineColumns = 4096;
    bool logging = false, timestampMetadata = false;
    QString logDirectory;
    QString logName = QStringLiteral("{profile}-{date}-{time}-{id}");
    QString logExtension = QStringLiteral(".log");
    QString rawLogExtension = QStringLiteral(".raw");
    QString logMode = QStringLiteral("text");
    bool rawLogging() const { return logMode == "raw" || logMode == "both"; }
    bool textLogging() const { return logMode == "text" || logMode == "both"; }
    bool terminalTimestamp = false, textTimestamp = false, timestampMilliseconds = false;
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
    QString defaultLogDirectory = QStringLiteral("logs");
    QString esptoolPath, esptoolDefaultChip;
    int esptoolBaud = 460800;
    bool confirmReset = true;
    bool load(const QString &file, QString &error);
    bool save(QString &error);
private:
    QStringList lines;
    bool utf8Bom = false, crlf = false;
};
