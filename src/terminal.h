#pragma once
#include <QByteArray>
#include <QString>
#include <QVector>
#include <QDateTime>

QString filterTerminalLines(const QString &text, const QString &query, Qt::CaseSensitivity sensitivity);
bool saveTerminalText(const QString &path, const QString &text, QString &error);

class Utf8Visible {
public:
    QString feed(const QByteArray &bytes, bool finish = false);
private:
    QByteArray pending;
};

class Terminal {
public:
    Terminal(int rows = 2000, int columns = 4096);
    void feed(const QByteArray &bytes, const QDateTime &received = QDateTime::currentDateTimeUtc());
    void localEcho(const QByteArray &bytes);
    void finish();
    void clear();
    void setLimits(int rows, int columns);
    QString plainText() const;
    QString html(bool timestamps = false, bool milliseconds = false) const;
    QString timestampPrefix(int line, bool milliseconds) const;
    int rowCount() const { return lines.size(); }
    int longestLine() const;
    qsizetype cellCount() const;
private:
    struct Cell { QChar ch = ' '; int color = -1; bool bold = false; };
    Utf8Visible decoder;
    QVector<QVector<Cell>> lines;
    QVector<QDateTime> receivedTimes;
    QDateTime currentReceived;
    int maxRows, maxColumns, row = 0, column = 0, color = -1;
    bool bold = false;
    enum Parse { Text, Escape, Csi, Osc, OscEscape } parse = Text;
    QString sequence;
    void consume(const QString &text);
    void put(QChar ch);
    void newLine();
    void csi(QChar command);
    void ensureRow(int target);
    void boundCells();
};
