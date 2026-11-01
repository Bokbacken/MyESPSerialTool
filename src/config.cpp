#include "config.h"
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

QMap<QString, QString> Profile::values() const {
    QMap<QString, QString> v;
    auto number = [&](const char *key, qint64 value) { v[key] = QString::number(value); };
    auto flag = [&](const char *key, bool value) { v[key] = value ? "true" : "false"; };
    v["port"] = port; v["usbSerial"] = usbSerial; v["parity"] = parity;
    v["stopBits"] = stopBits; v["flowControl"] = flowControl; v["newline"] = newline;
    v["logDirectory"] = logDirectory;
    number("baud", baud); number("dataBits", dataBits); number("vid", vid); number("pid", pid);
    number("writeTimeoutMs", writeTimeoutMs); number("retryLimit", retryLimit);
    number("terminalRows", terminalRows); number("lineColumns", lineColumns);
    number("logBytes", logBytes); number("logRetention", logRetention);
    number("logQueueBytes", logQueueBytes);
    flag("readOnly", readOnly); flag("dtr", dtr); flag("rts", rts);
    flag("localEcho", localEcho); flag("follow", follow); flag("logging", logging);
    flag("timestampMetadata", timestampMetadata);
    return v;
}

bool Profile::fromValues(const QString &name, const QMap<QString, QString> &v,
                         Profile &out, QString &error) {
    Profile p; p.name = name;
    auto fail = [&](const QString &key) {
        error = QStringLiteral("Profile '%1': invalid %2").arg(name, key); return false;
    };
    if (name.isEmpty() || name != name.trimmed() || name.size() > 100
        || name.contains(QRegularExpression("[\\r\\n\\[\\]=\\x00]")))
        return fail("name");
    auto num = [&](const char *key, qint64 lo, qint64 hi, auto &target) {
        if (!v.contains(key)) return true;
        const auto s = v.value(key);
        bool ok = false; const qint64 n = s.toLongLong(&ok);
        if (!ok || !QRegularExpression("^-?[0-9]+$").match(s).hasMatch() || n < lo || n > hi)
            return fail(key);
        target = n; return true;
    };
    auto boolean = [&](const char *key, bool &target) {
        if (!v.contains(key)) return true;
        if (v[key] != "true" && v[key] != "false") return fail(key);
        target = v[key] == "true"; return true;
    };
    auto choice = [&](const char *key, QStringList options, QString &target) {
        if (!v.contains(key)) return true;
        if (!options.contains(v[key])) return fail(key);
        target = v[key]; return true;
    };
    if (!num("baud", 1, 4000000, p.baud) || !num("dataBits", 5, 8, p.dataBits)
        || !num("vid", -1, 65535, p.vid) || !num("pid", -1, 65535, p.pid)
        || !num("writeTimeoutMs", 100, 60000, p.writeTimeoutMs)
        || !num("retryLimit", 1, 100, p.retryLimit)
        || !num("terminalRows", 10, 10000, p.terminalRows)
        || !num("lineColumns", 32, 16384, p.lineColumns)
        || !num("logBytes", 1, 1073741824, p.logBytes)
        || !num("logRetention", 1, 100, p.logRetention)
        || !num("logQueueBytes", 1, 67108864, p.logQueueBytes)
        || !choice("parity", {"none", "even", "odd", "space", "mark"}, p.parity)
        || !choice("stopBits", {"1", "1.5", "2"}, p.stopBits)
        || !choice("flowControl", {"none", "hardware", "software"}, p.flowControl)
        || !choice("newline", {"none", "lf", "cr", "crlf"}, p.newline)
        || !boolean("readOnly", p.readOnly) || !boolean("dtr", p.dtr) || !boolean("rts", p.rts)
        || !boolean("localEcho", p.localEcho) || !boolean("follow", p.follow)
        || !boolean("logging", p.logging) || !boolean("timestampMetadata", p.timestampMetadata))
        return false;
    for (const auto &key : {"port", "usbSerial", "logDirectory"}) {
        const QString s = v.value(key, p.values().value(key));
        if (s.size() > 4096 || s.contains(QRegularExpression("[\\r\\n\\x00]"))
            || s != s.trimmed() || s.contains(QRegularExpression("\\s[;#]")))
            return fail(key);
        if (QString(key) == "port") p.port = s;
        else if (QString(key) == "usbSerial") p.usbSerial = s;
        else p.logDirectory = s;
    }
    if (p.logDirectory.isEmpty()) return fail("logDirectory");
    if ((!p.usbSerial.isEmpty() && (p.vid < 0 || p.pid < 0))
        || (p.usbSerial.isEmpty() && (p.vid >= 0 || p.pid >= 0)))
        return fail("USB identity (serial, VID and PID must all be present or all absent)");
    out = p; return true;
}

static QString valuePart(const QString &s) {
    const auto m = QRegularExpression("\\s[;#]").match(s);
    return (m.hasMatch() ? s.left(m.capturedStart()) : s).trimmed();
}

bool IniFile::load(const QString &file, QString &error) {
    QFile input(file);
    if (!input.exists()) {
        path = file; lines.clear(); profiles = {Profile{}}; utf8Bom = crlf = false; return true;
    }
    if (!input.open(QIODevice::ReadOnly)) { error = input.errorString(); return false; }
    if (input.size() > 4194304) { error = "INI exceeds 4 MiB size limit"; return false; }
    const QByteArray originalBytes = input.readAll();
    if (input.error() != QFileDevice::NoError) { error = input.errorString(); return false; }
    const bool bom = originalBytes.startsWith(QByteArray::fromHex("efbbbf"));
    const QByteArray bytes = bom ? originalBytes.mid(3) : originalBytes;
    const QString text = QString::fromUtf8(bytes);
    if (text.toUtf8() != bytes || text.contains(QChar(0))) {
        error = "INI must be valid UTF-8 without NUL bytes"; return false;
    }
    QStringList parsedLines = text.split('\n');
    for (QString &line : parsedLines) if (line.endsWith('\r')) line.chop(1);
    QMap<QString, QMap<QString, QString>> sections;
    QStringList order;
    QString section;
    int lineNumber = 0;
    for (const auto &line : parsedLines) {
        ++lineNumber;
        const auto trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(';') || trimmed.startsWith('#')) continue;
        if (trimmed.startsWith('[')) {
            const auto m = QRegularExpression("^\\[([^\\[\\]]+)\\]\\s*(?:[;#].*)?$").match(trimmed);
            if (!m.hasMatch()) { error = QString("Malformed section at line %1").arg(lineNumber); return false; }
            section = m.captured(1);
            if (sections.contains(section)) { error = "Duplicate INI section: " + section; return false; }
            sections[section] = {};
            order << section;
        } else {
            const int equals = line.indexOf('=');
            if (equals <= 0 || section.isEmpty()) {
                error = QString("Expected section/key=value at line %1").arg(lineNumber); return false;
            }
            const QString key = line.left(equals).trimmed();
            if (key.isEmpty() || sections[section].contains(key)) {
                error = QString("Empty or duplicate key at line %1").arg(lineNumber); return false;
            }
            sections[section][key] = valuePart(line.mid(equals + 1));
        }
    }
    if (sections.value("app").value("format", "1") != "1") {
        error = "Unsupported configuration format (expected 1)"; return false;
    }
    QList<Profile> result;
    for (const QString &s : order) {
        if (!s.startsWith("profile:")) continue;
        Profile p;
        if (!Profile::fromValues(s.mid(8), sections[s], p, error)) return false;
        result << p;
    }
    if (result.isEmpty()) result << Profile{};
    path = file; lines = parsedLines; profiles = result;
    utf8Bom = bom; crlf = bytes.contains("\r\n"); return true;
}

static bool atomicWrite(const QString &path, const QByteArray &data, QString &error) {
    QSaveFile f(path);
    f.setDirectWriteFallback(false);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        error = QString("%1: %2").arg(path, f.errorString()); return false;
    }
    return true;
}

bool IniFile::save(QString &error) {
    QMap<QString, QMap<QString, QString>> updates;
    updates["app"]["format"] = "1";
    QSet<QString> names;
    for (const Profile &p : profiles) {
        Profile validated;
        if (!Profile::fromValues(p.name, p.values(), validated, error)) return false;
        if (names.contains(p.name)) { error = "Duplicate profile name: " + p.name; return false; }
        names.insert(p.name); updates["profile:" + p.name] = p.values();
    }
    QStringList output;
    QString current;
    auto appendMissing = [&] {
        if (!updates.contains(current)) return;
        for (auto it = updates[current].cbegin(); it != updates[current].cend(); ++it)
            output << it.key() + "=" + it.value();
        updates.remove(current);
    };
    for (const QString &line : lines) {
        const auto m = QRegularExpression("^\\s*\\[([^\\[\\]]+)\\]").match(line);
        if (m.hasMatch()) { appendMissing(); current = m.captured(1); }
        const int eq = line.indexOf('=');
        const QString key = eq > 0 ? line.left(eq).trimmed() : QString();
        if (!line.trimmed().startsWith(';') && !line.trimmed().startsWith('#')
            && updates.value(current).contains(key)) {
            const QString rhs = line.mid(eq + 1);
            const auto comment = QRegularExpression("\\s[;#]").match(rhs);
            output << line.left(eq + 1) + updates[current].take(key)
                      + (comment.hasMatch() ? rhs.mid(comment.capturedStart()) : QString());
        } else output << line;
    }
    appendMissing();
    for (auto it = updates.cbegin(); it != updates.cend(); ++it) {
        output << "" << "[" + it.key() + "]";
        for (auto kv = it->cbegin(); kv != it->cend(); ++kv) output << kv.key() + "=" + kv.value();
    }
    while (!output.isEmpty() && output.last().isEmpty()) output.removeLast();
    QByteArray data = (output.join(crlf ? "\r\n" : "\n") + (crlf ? "\r\n" : "\n")).toUtf8();
    if (utf8Bom) data.prepend(QByteArray::fromHex("efbbbf"));
    if (QFileInfo::exists(path)) {
        QFile old(path);
        if (!old.open(QIODevice::ReadOnly)) { error = old.errorString(); return false; }
        const QByteArray previous = old.readAll();
        if (old.error() != QFileDevice::NoError) { error = old.errorString(); return false; }
        if (!atomicWrite(path + ".bak", previous, error)) return false;
    }
    if (!atomicWrite(path, data, error)) return false;
    lines = output; return true;
}
