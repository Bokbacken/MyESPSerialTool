#include "terminal.h"
#include <algorithm>
#include <QSaveFile>
#include <QStringList>

QString filterTerminalLines(const QString &text, const QString &query, Qt::CaseSensitivity sensitivity) {
    if (query.isEmpty()) return text;
    QStringList matches;
    for (const auto &line : text.split('\n'))
        if (line.contains(query, sensitivity)) matches << line;
    return matches.join('\n');
}
bool saveTerminalText(const QString &path, const QString &text, QString &error) {
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    const auto bytes = text.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = "Cannot save terminal export: " + path + ": " + file.errorString();
        return false;
    }
    return true;
}

static QString visibleByte(unsigned char b) {
    return QString("\\x%1").arg(b, 2, 16, QLatin1Char('0')).toUpper().replace("\\X", "\\x");
}

QString Utf8Visible::feed(const QByteArray &bytes, bool finish) {
    pending += bytes;
    QString out;
    qsizetype i = 0;
    while (i < pending.size()) {
        const auto b = static_cast<unsigned char>(pending[i]);
        if (b < 0x80) { out += QChar(b); ++i; continue; }
        const int count = b >= 0xc2 && b <= 0xdf ? 2 : b >= 0xe0 && b <= 0xef ? 3
                        : b >= 0xf0 && b <= 0xf4 ? 4 : 0;
        if (!count) { out += visibleByte(b); ++i; continue; }
        bool invalid = false;
        const int available = std::min<qsizetype>(count, pending.size() - i);
        for (int n = 1; n < available; ++n) {
            const auto next = static_cast<unsigned char>(pending[i + n]);
            if (next < 0x80 || next > 0xbf || (n == 1 &&
                ((b == 0xe0 && next < 0xa0) || (b == 0xed && next >= 0xa0)
                 || (b == 0xf0 && next < 0x90) || (b == 0xf4 && next >= 0x90))))
                invalid = true;
        }
        if (!invalid && available < count && !finish) break;
        if (invalid || available < count) { out += visibleByte(b); ++i; continue; }
        out += QString::fromUtf8(pending.constData() + i, count); i += count;
    }
    pending.remove(0, i);
    return out;
}

Terminal::Terminal(int rows, int columns)
    : maxRows(std::clamp(rows, 1, 10000)), maxColumns(std::clamp(columns, 1, 16384)) {
    lines.append(QVector<Cell>{});
}
void Terminal::clear() {
    lines = {QVector<Cell>{}}; row = column = 0; color = -1; bold = false;
    parse = Text; sequence.clear(); decoder = Utf8Visible{};
}
void Terminal::setLimits(int rows, int columns) {
    maxRows = std::clamp(rows, 1, 10000);
    maxColumns = std::clamp(columns, 1, 16384);
    const int removed = qMax(0, int(lines.size()) - maxRows);
    if (removed > 0) lines.remove(0, removed);
    row = qMax(0, row - removed); column = qMin(column, maxColumns);
    for (auto &line : lines)
        if (line.size() > maxColumns) line.resize(maxColumns);
    boundCells();
}
void Terminal::feed(const QByteArray &bytes) {
    // Chunk even direct callers so bounds do not depend on the transport's chunk size.
    for (qsizetype start = 0; start < bytes.size(); start += 16384) {
        consume(decoder.feed(bytes.mid(start, 16384)));
        boundCells();
    }
}
void Terminal::localEcho(const QByteArray &bytes) {
    // Local TX must not complete a partially received UTF-8 character or ANSI sequence.
    const auto savedParse = parse;
    const auto savedSequence = sequence;
    const int savedColor = color;
    const bool savedBold = bold;
    parse = Text; sequence.clear(); color = -1; bold = false;
    Utf8Visible local;
    consume(local.feed(bytes, true));
    boundCells();
    parse = savedParse; sequence = savedSequence; color = savedColor; bold = savedBold;
}
void Terminal::finish() {
    consume(decoder.feed({}, true));
    if (parse == Escape || parse == Csi) {
        const QString unfinished = QStringLiteral("\\x1B") + (parse == Csi ? "[" + sequence : "");
        parse = Text; consume(unfinished);
    }
    parse = Text; sequence.clear();
    boundCells();
}
void Terminal::ensureRow(int target) {
    target = std::clamp(target, 0, maxRows - 1);
    while (lines.size() <= target) lines.append(QVector<Cell>{});
    row = target;
}
void Terminal::newLine() {
    column = 0;
    if (row + 1 >= maxRows) { lines.removeFirst(); lines.append(QVector<Cell>{}); row = maxRows - 1; }
    else ensureRow(row + 1);
}
void Terminal::put(QChar ch) {
    if (column >= maxColumns) newLine();
    auto &line = lines[row];
    while (line.size() <= column) line.append(Cell{});
    line[column++] = {ch, color, bold};
}
void Terminal::consume(const QString &text) {
    for (QChar ch : text) {
        if (parse == Osc || parse == OscEscape) {
            if (ch == QChar(7) || (parse == OscEscape && ch == '\\')) { parse = Text; sequence.clear(); }
            else if (sequence.size() >= 256) { parse = Text; sequence.clear(); }
            else { sequence += ch; parse = ch == QChar(27) ? OscEscape : Osc; }
            continue;
        }
        if (parse == Escape) {
            if (ch == '[') { parse = Csi; sequence.clear(); }
            else if (ch == ']') { parse = Osc; sequence.clear(); }
            else { parse = Text; consume("\\x1B"); if (ch != QChar(27)) put(ch); }
            continue;
        }
        if (parse == Csi) {
            if (ch.unicode() >= 0x40 && ch.unicode() <= 0x7e) { csi(ch); parse = Text; sequence.clear(); }
            else if (sequence.size() >= 64) { parse = Text; sequence.clear(); consume("[invalid ANSI]"); }
            else sequence += ch;
            continue;
        }
        if (ch == QChar(27)) parse = Escape;
        else if (ch == '\n') newLine();
        else if (ch == '\r') column = 0;
        else if (ch == '\b') column = std::max(0, column - 1);
        else if (ch == '\t') {
            const int spaces = 8 - (column % 8);
            for (int i = 0; i < spaces; ++i) put(' ');
        } else if (ch.unicode() < 32 || ch.unicode() == 127) {
            for (QChar c : visibleByte(ch.unicode())) put(c);
        } else put(ch);
    }
}
void Terminal::csi(QChar command) {
    if (sequence.contains('?') || sequence.contains('>')) return;
    const QStringList args = sequence.split(';');
    QVector<int> params;
    for (const QString &arg : args) {
        bool ok = false; int n = arg.isEmpty() ? 0 : arg.toInt(&ok);
        if (!arg.isEmpty() && !ok) return;
        params << std::clamp(n, 0, 16384);
    }
    const int n = params.value(0, 0);
    const int distance = n == 0 ? 1 : n;
    if (command == 'm') {
        for (int p : params) {
            if (p == 0) { color = -1; bold = false; }
            else if (p == 1) bold = true;
            else if (p == 22) bold = false;
            else if (p == 39) color = -1;
            else if (p >= 30 && p <= 37) color = p - 30;
            else if (p >= 90 && p <= 97) color = p - 90 + 8;
        }
    } else if (command == 'A') row = std::max(0, row - distance);
    else if (command == 'B') ensureRow(row + distance);
    else if (command == 'C') column = std::min(maxColumns - 1, column + distance);
    else if (command == 'D') column = std::max(0, column - distance);
    else if (command == 'G') column = std::clamp(distance - 1, 0, maxColumns - 1);
    else if (command == 'H' || command == 'f') {
        ensureRow(distance - 1);
        column = std::clamp(std::max(1, params.value(1, 1)) - 1, 0, maxColumns - 1);
    } else if (command == 'K') {
        if (n == 0) lines[row].resize(std::min(column, int(lines[row].size())));
        else if (n == 1) {
            for (int i = 0; i <= column && i < lines[row].size(); ++i) lines[row][i] = Cell{};
        } else if (n == 2) lines[row].clear();
    } else if (command == 'J') {
        if (n == 2 || n == 3) { for (auto &line : lines) line.clear(); }
        else if (n == 0) {
            lines[row].resize(std::min(column, int(lines[row].size())));
            for (int i = row + 1; i < lines.size(); ++i) lines[i].clear();
        } else if (n == 1) {
            for (int i = 0; i < row; ++i) lines[i].clear();
            for (int i = 0; i <= column && i < lines[row].size(); ++i) lines[row][i] = Cell{};
        }
    }
}
QString Terminal::plainText() const {
    QStringList out;
    for (const auto &line : lines) {
        QString s; for (const auto &cell : line) s += cell.ch; out << s;
    }
    return out.join('\n');
}
int Terminal::longestLine() const {
    int n = 0; for (const auto &line : lines) n = std::max(n, int(line.size())); return n;
}
qsizetype Terminal::cellCount() const {
    qsizetype total = 0;
    for (const auto &line : lines) total += line.size();
    return total;
}
void Terminal::boundCells() {
    qsizetype cells = cellCount();
    while (cells > 1048576 && lines.size() > 1) {
        if (row > 0) {
            cells -= lines.first().size(); lines.removeFirst(); --row;
        } else {
            cells -= lines.last().size(); lines.removeLast();
        }
    }
}
QString Terminal::html() const {
    static const QStringList colors = {"#777777", "#cd5555", "#55bb55", "#bbbb55",
        "#6699ee", "#bb66bb", "#55bbbb", "#dddddd", "#aaaaaa", "#ff7777", "#77ff77",
        "#ffff77", "#99bbff", "#ff99ff", "#99ffff", "#ffffff"};
    QString out = "<pre style=\"white-space:pre; margin:0;\">";
    for (int r = 0; r < lines.size(); ++r) {
        int previousColor = -2; bool previousBold = false;
        bool span = false;
        for (const Cell &cell : lines[r]) {
            if (cell.color != previousColor || cell.bold != previousBold) {
                if (span) out += "</span>";
                out += QString("<span style=\"color:%1;font-weight:%2\">")
                    .arg(cell.color < 0 ? "#dddddd" : colors[cell.color], cell.bold ? "bold" : "normal");
                previousColor = cell.color; previousBold = cell.bold; span = true;
            }
            out += QString(cell.ch).toHtmlEscaped();
        }
        if (span) out += "</span>";
        if (r + 1 < lines.size()) out += '\n';
    }
    return out + "</pre>";
}
