#include "filter.h"
#include "config.h"
#include "logger.h"
#include "terminal.h"
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class FilterTests : public QObject {
    Q_OBJECT
private slots:
    void expressions_data() {
        QTest::addColumn<QString>("expression");
        QTest::addColumn<QString>("line");
        QTest::addColumn<bool>("expected");
        QTest::newRow("literal-substring") << "\"hello\"" << "before HELLO after" << true;
        QTest::newRow("literal-missing") << "\"hello\"" << "goodbye" << false;
        QTest::newRow("empty") << "" << "anything" << true;
        QTest::newRow("empty-quotes") << "\"\"" << "" << true;
        QTest::newRow("wildcard-user-example") << "\"ord??tex*detta\"" << "ordettexharvadsomhelsthärdetta" << true;
        QTest::newRow("question-too-short") << "\"ord??tex*detta\"" << "ordetexdetta" << false;
        QTest::newRow("star-zero") << "\"a*b\"" << "ab" << true;
        QTest::newRow("star-many") << "\"a*b\"" << "___a12345b___" << true;
        QTest::newRow("star-all") << "\"***\"" << "" << true;
        QTest::newRow("star-retry") << "\"ab*ab?c\"" << "ababXababZc" << true;
        QTest::newRow("escaped-wildcards") << "\"a\\?b\\*c\"" << "a?b*c" << true;
        QTest::newRow("escaped-not-wild") << "\"a\\?b\\*c\"" << "aXbYYc" << false;
        QTest::newRow("escapes") << "\"quote\\\"path\\\\end\"" << "quote\"path\\end" << true;
        QTest::newRow("unicode-one") << QString::fromUtf8("\"a?b\"") << QString::fromUtf8("a😀b") << true;
        QTest::newRow("unicode-fold") << QString::fromUtf8("\"Å?Ö\"") << QString::fromUtf8("åäö") << true;
        QTest::newRow("unicode-one-fold") << "\"a?b\"" << QString::fromUtf8("aßb") << true;
        QTest::newRow("and-precedence") << "\"a\" || \"b\" && \"c\"" << "a" << true;
        QTest::newRow("and-precedence-false") << "\"a\" || \"b\" && \"c\"" << "b" << false;
        QTest::newRow("parentheses") << "(\"a\" || \"b\") && \"c\"" << "a" << false;
        QTest::newRow("not-precedence") << "!\"a\" && \"b\"" << "ab" << false;
        QTest::newRow("not-group") << "!(\"a\" || \"b\")" << "c" << true;
        QTest::newRow("double-not") << "!!\"a\"" << "a" << true;
        QTest::newRow("field-equals") << "temp >= 20 && temp < 30" << "temp=23.5" << true;
        QTest::newRow("field-colon") << "temp >= 20 && temp < 30" << "TEMP : 29.99" << true;
        QTest::newRow("field-upper-bound") << "temp >= 20 && temp < 30" << "temp=30" << false;
        QTest::newRow("field-lower-bound") << "temp >= 20 && temp < 30" << "temp=19.99" << false;
        QTest::newRow("negative-exponent") << "value == -1.5e2" << "value=-150" << true;
        QTest::newRow("plus-exponent") << "value == +3E-2" << "value: .03" << true;
        QTest::newRow("not-equal") << "value != 0" << "value=-1" << true;
        QTest::newRow("missing-not-equal") << "value != 0" << "other=4" << false;
        QTest::newRow("invalid-not-equal") << "value != 0" << "value=NaN" << false;
        QTest::newRow("overflow-value") << "value > 0" << "value=1e999" << false;
        QTest::newRow("missing-negated") << "!(value > 0)" << "other=4" << true;
        QTest::newRow("exact-field-name") << "temp == 23" << "othertemp=23" << false;
        QTest::newRow("dotted-field") << "sensor.temp <= 23" << "sensor.temp:23" << true;
        QTest::newRow("repeat-first") << "temp == 23" << "temp=23 temp=99" << true;
        QTest::newRow("unit-rejected") << "temp == 23" << "temp=23C" << false;
        QTest::newRow("unicode-unit-rejected") << "temp == 23" << QString::fromUtf8("temp=23å") << false;
        QTest::newRow("unicode-field-boundary") << "temp == 23" << QString::fromUtf8("åtemp=23") << false;
        QTest::newRow("malformed-decimal") << "temp == 23" << "temp=23.4.5" << false;
        QTest::newRow("malformed-exponent") << "temp == 23" << "temp=23e+" << false;
        QTest::newRow("field-number-name") << "number == 23" << "number=23" << true;
        QTest::newRow("position") << "number(2) >= 3 && number(2) <= 4" << "123, 3.3, -99" << true;
        QTest::newRow("position-skips-identifiers") << "number(1) == 23" << "ESP32 temp=23" << true;
        QTest::newRow("position-signed") << "number(2) == -.5" << "1 -0.5" << true;
        QTest::newRow("position-exponent") << "number(2) == 200" << "1, 2e2" << true;
        QTest::newRow("position-missing") << "number(3) != 0" << "1 2" << false;
        QTest::newRow("mixed") << "\"sensor*\" && voltage >= 3.1 && number(1) < 10" << "sensor voltage=3.3" << true;
    }
    void expressions() {
        QFETCH(QString, expression); QFETCH(QString, line); QFETCH(bool, expected);
        LineFilter filter; QString error;
        QVERIFY2(filter.compile(expression, Qt::CaseInsensitive, error), qPrintable(error));
        QCOMPARE(filter.matches(line), expected);
    }
    void invalid_data() {
        QTest::addColumn<QString>("expression");
        for (const auto &expression : {"word", "\"unterminated", "\"x\" &&", "()",
            "(\"x\"", "\"x\")", "\"x\" \"y\"", "\"x\" & \"y\"", "\"x\" | \"y\"",
            "\"\\z\"", "\"abc\\", "temp = 3", "temp > NaN", "temp > 1e999",
            "temp > 1,5", "temp > 1e", "number(0) > 1", "number(-1)>1",
            "number(1001)>1", "number(99999999999999999)>1", "number(2.5)>1",
            "number(2 > 1", "number()>1", "number(1) >", "temp => 4",
            "temp >= 3 junk", "temp >= 3; shell", "temp >= inf"})
            QTest::newRow(expression) << QString(expression);
    }
    void invalid() {
        QFETCH(QString, expression);
        LineFilter filter; QString error;
        QVERIFY(!filter.compile(expression, Qt::CaseInsensitive, error));
        QVERIFY2(error.startsWith("Position "), qPrintable(error));
        QVERIFY(!filter.matches("anything"));
    }
    void limitsAndRecovery() {
        LineFilter filter; QString error;
        QVERIFY(filter.compile(QString(4096, ' '), Qt::CaseInsensitive, error));
        QVERIFY(!filter.compile(QString(4097, 'x'), Qt::CaseInsensitive, error));
        QVERIFY(filter.compile(QString(32, '!') + "\"x\"", Qt::CaseInsensitive, error));
        QVERIFY(filter.compile(QString(32, '(') + "\"x\"" + QString(32, ')'), Qt::CaseInsensitive, error));
        QVERIFY(!filter.compile(QString(33, '!') + "\"x\"", Qt::CaseInsensitive, error));
        QVERIFY(!filter.compile(QString(33, '(') + "\"x\"" + QString(33, ')'), Qt::CaseInsensitive, error));
        QString many;
        for (int i = 0; i < 80; ++i) many += (i ? " || " : "") + QString("\"x\"");
        QVERIFY(!filter.compile(many, Qt::CaseInsensitive, error));
        QVERIFY(error.contains("128"));
        many.clear();
        for (int i = 0; i < 64; ++i) many += (i ? " || " : "!") + QString("\"x\"");
        QVERIFY(filter.compile(many, Qt::CaseInsensitive, error));
        QVERIFY(!filter.compile("!" + many, Qt::CaseInsensitive, error));
        QVERIFY(filter.compile("\"good\"", Qt::CaseSensitive, error));
        QVERIFY(error.isEmpty()); QVERIFY(filter.matches("good")); QVERIFY(!filter.matches("GOOD"));
        QVERIFY(filter.compile("temp == 1", Qt::CaseSensitive, error));
        QVERIFY(!filter.matches("TEMP=1"));
        QVERIFY(filter.compile("\"a*\"", Qt::CaseInsensitive, error));
        QVERIFY(filter.matches("a" + QString(16384, 'x')));
    }
    void wildcardCombinationsAgreeWithReference() {
        QStringList patterns = {""}, lines = {""};
        QStringList level = {""};
        for (int length = 1; length <= 4; ++length) {
            QStringList next;
            for (const auto &prefix : level)
                for (QChar ch : QString("ab?*")) next << prefix + ch;
            patterns += next; level = next;
        }
        level = {""};
        for (int length = 1; length <= 4; ++length) {
            QStringList next;
            for (const auto &prefix : level)
                for (QChar ch : QString("ab")) next << prefix + ch;
            lines += next; level = next;
        }
        for (const auto &pattern : patterns) {
            QString regex;
            for (QChar ch : pattern)
                regex += ch == '*' ? ".*" : ch == '?' ? "." : QString(ch);
            const QRegularExpression reference(regex);
            LineFilter filter; QString error;
            QVERIFY(filter.compile("\"" + pattern + "\"", Qt::CaseSensitive, error));
            for (const auto &line : lines)
                QCOMPARE(filter.matches(line), reference.match(line).hasMatch());
        }
    }
    void profileAndRawLogRemainIndependent() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        Profile p; p.logMode = "raw"; p.filterMode = "expression"; p.filterText = "\"error*\" && temp > 20";
        p.filterCase = true; p.filterHighlight = true; p.logging = true;
        IniFile ini; QString error;
        QVERIFY(ini.load(directory.filePath("settings.ini"), error));
        ini.profiles = {p}; QVERIFY2(ini.save(error), qPrintable(error));
        IniFile restored; QVERIFY(restored.load(ini.path, error));
        QCOMPARE(restored.profiles.first().values(), p.values());
        Profile invalid;
        QVERIFY(!Profile::fromValues("ESP", {{"filterMode", "expression"}, {"filterText", "\"x\" &&"}}, invalid, error));
        const auto base = directory.filePath("capture");
        const auto time = QDateTime::currentDateTimeUtc();
        const QByteArray bytes = "info temp=1\n\x1b[31merror flash temp=23\x1b[0m\n";
        RotatingSink sink(p, base);
        QVERIFY(sink.append(bytes, time, error));
        Terminal terminal; terminal.feed(bytes);
        LineFilter filter;
        QVERIFY(filter.compile(p.filterText, Qt::CaseSensitive, error));
        int count = 0;
        const auto selected = filter.select(terminal.plainText(), &count);
        QCOMPARE(count, 1); QCOMPARE(selected, QString("error flash temp=23"));
        QVERIFY(saveTerminalText(directory.filePath("export.txt"), selected, error));
        QVERIFY(sink.finish("finished", time, error));
        QFile raw(base + ".raw"); QVERIFY(raw.open(QIODevice::ReadOnly));
        QCOMPARE(raw.readAll(), bytes);
    }
};
QTEST_GUILESS_MAIN(FilterTests)
#include "test_filter.moc"
