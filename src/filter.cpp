#include "filter.h"
#include <QStringList>
#include <cmath>

namespace {
constexpr uint anyCharacter = 0x110000;
constexpr uint anySequence = 0x110001;
const QString numberPattern = QStringLiteral("[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?");

bool wildcardMatch(const QVector<uint> &pattern, const QString &line, Qt::CaseSensitivity sensitivity) {
    const auto text = line.toUcs4();
    qsizetype p = 0, t = 0, star = -1, retry = 0;
    while (t < text.size()) {
        if (p < pattern.size() && pattern[p] == anySequence) {
            star = p++; retry = t;
        } else if (p < pattern.size() && (pattern[p] == anyCharacter || pattern[p] == text[t]
            || (sensitivity == Qt::CaseInsensitive && pattern[p] <= 0x10ffff
                && QChar::toCaseFolded(pattern[p]) == QChar::toCaseFolded(text[t])))) {
            ++p; ++t;
        } else if (star >= 0) {
            p = star + 1; t = ++retry;
        } else return false;
    }
    while (p < pattern.size() && pattern[p] == anySequence) ++p;
    return p == pattern.size();
}
}

void LineFilter::spaces() {
    while (position < source.size() && source[position].isSpace()) ++position;
}
bool LineFilter::take(const QString &token) {
    spaces();
    if (source.mid(position, token.size()) != token) return false;
    position += token.size(); return true;
}
int LineFilter::fail(const QString &message) {
    if (problem.isEmpty()) problem = QString("Position %1: %2").arg(position + 1).arg(message);
    return -1;
}
int LineFilter::add(Node node) {
    if (nodes.size() >= 128) return fail("Filter is too complex (maximum 128 terms/operators)");
    nodes.append(std::move(node)); return nodes.size() - 1;
}
bool LineFilter::compile(const QString &expression, Qt::CaseSensitivity sensitivity, QString &error) {
    source = expression; position = 0; nodes.clear(); problem.clear(); root = -1;
    caseSensitivity = sensitivity; error.clear();
    if (source.size() > 4096) { fail("Maximum expression length is 4096 characters"); error = problem; return false; }
    spaces();
    if (position == source.size()) return true;
    const int parsed = parseOr(0);
    spaces();
    if (parsed >= 0 && position != source.size()) fail("Expected &&, || or end of expression");
    if (!problem.isEmpty()) { nodes.clear(); error = problem; return false; }
    root = parsed; return true;
}
int LineFilter::parseOr(int depth) {
    int left = parseAnd(depth);
    while (left >= 0 && take("||")) {
        const int right = parseAnd(depth);
        if (right < 0) return -1;
        Node node; node.kind = Kind::Or; node.left = left; node.right = right;
        left = add(node);
    }
    return left;
}
int LineFilter::parseAnd(int depth) {
    int left = parseUnary(depth);
    while (left >= 0 && take("&&")) {
        const int right = parseUnary(depth);
        if (right < 0) return -1;
        Node node; node.kind = Kind::And; node.left = left; node.right = right;
        left = add(node);
    }
    return left;
}
int LineFilter::parseUnary(int depth) {
    if (depth > 32) return fail("Maximum nesting depth is 32");
    if (take("!")) {
        const int child = parseUnary(depth + 1);
        if (child < 0) return -1;
        Node node; node.kind = Kind::Not; node.left = child; return add(node);
    }
    if (take("(")) {
        const int child = parseOr(depth + 1);
        if (child < 0) return -1;
        if (!take(")")) return fail("Expected )");
        return child;
    }
    return parseAtom();
}
int LineFilter::parseAtom() {
    spaces();
    if (take("\"")) {
        Node node;
        node.pattern << anySequence;
        QString literal;
        auto flush = [&] {
            node.pattern += literal.toUcs4(); literal.clear();
        };
        while (position < source.size()) {
            const QChar ch = source[position++];
            if (ch == '"') {
                flush(); node.pattern << anySequence; return add(node);
            }
            if (ch == '\\') {
                if (position == source.size()) return fail("Incomplete escape");
                const QChar escaped = source[position++];
                if (escaped != '\\' && escaped != '"' && escaped != '*' && escaped != '?')
                    return fail("Allowed escapes: \\\\, \\\", \\*, \\?");
                literal += escaped;
            } else if (ch == '*' || ch == '?') {
                flush();
                const auto token = ch == '*' ? anySequence : anyCharacter;
                if (token != anySequence || node.pattern.last() != anySequence) node.pattern << token;
            } else literal += ch;
        }
        return fail("Unterminated quoted search term");
    }
    static const QRegularExpression identifier("[A-Za-z_][A-Za-z0-9_.-]*");
    const auto name = identifier.match(source, position, QRegularExpression::NormalMatch,
                                       QRegularExpression::AnchorAtOffsetMatchOption);
    if (!name.hasMatch()) return fail("Expected a quoted search term, field comparison or number(N)");
    position = name.capturedEnd();
    Node node; node.kind = Kind::Number;
    if (name.captured() == "number" && take("(")) {
        static const QRegularExpression index("[0-9]+");
        spaces();
        const auto match = index.match(source, position, QRegularExpression::NormalMatch,
                                        QRegularExpression::AnchorAtOffsetMatchOption);
        if (!match.hasMatch()) return fail("Expected a number position (1-1000)");
        bool ok;
        node.numberIndex = match.captured().toInt(&ok);
        position = match.capturedEnd();
        if (!ok || node.numberIndex < 1 || node.numberIndex > 1000) return fail("Number position must be 1-1000");
        if (!take(")")) return fail("Expected ) after number position");
    } else {
        node.field = QRegularExpression("(?<![\\w.-])" + QRegularExpression::escape(name.captured())
            + "\\s*[=:]\\s*(" + numberPattern + ")(?![\\w.+-])",
            QRegularExpression::UseUnicodePropertiesOption
                | (caseSensitivity == Qt::CaseInsensitive ? QRegularExpression::CaseInsensitiveOption
                                                          : QRegularExpression::NoPatternOption));
    }
    for (const auto &op : {">=", "<=", "==", "!=", ">", "<"}) {
        if (take(op)) { node.comparison = op; break; }
    }
    if (node.comparison.isEmpty()) return fail("Expected a numeric comparison: < <= == != >= >");
    spaces();
    static const QRegularExpression value(numberPattern);
    const auto match = value.match(source, position, QRegularExpression::NormalMatch,
                                   QRegularExpression::AnchorAtOffsetMatchOption);
    if (!match.hasMatch()) return fail("Expected a finite decimal number (use a decimal point)");
    bool ok;
    node.value = match.captured().toDouble(&ok);
    position = match.capturedEnd();
    if (!ok || !std::isfinite(node.value)) return fail("Number is out of range");
    return add(node);
}
bool LineFilter::evaluate(int index, const QString &line) const {
    const auto &node = nodes[index];
    switch (node.kind) {
    case Kind::Not: return !evaluate(node.left, line);
    case Kind::And: return evaluate(node.left, line) && evaluate(node.right, line);
    case Kind::Or: return evaluate(node.left, line) || evaluate(node.right, line);
    case Kind::Text:
        return wildcardMatch(node.pattern, line, caseSensitivity);
    case Kind::Number: break;
    }
    QString numeric;
    if (node.numberIndex) {
        static const QRegularExpression numbers("(?<![\\w.+-])(" + numberPattern + ")(?![\\w.+-])",
                                                QRegularExpression::UseUnicodePropertiesOption);
        auto matches = numbers.globalMatch(line);
        for (int i = 0; i < node.numberIndex; ++i) {
            if (!matches.hasNext()) return false;
            numeric = matches.next().captured(1);
        }
    } else {
        const auto match = node.field.match(line);
        if (!match.hasMatch()) return false;
        numeric = match.captured(1);
    }
    bool ok;
    const double value = numeric.toDouble(&ok);
    if (!ok || !std::isfinite(value)) return false;
    if (node.comparison == ">=") return value >= node.value;
    if (node.comparison == "<=") return value <= node.value;
    if (node.comparison == ">") return value > node.value;
    if (node.comparison == "<") return value < node.value;
    if (node.comparison == "==") return value == node.value;
    return value != node.value;
}
bool LineFilter::matches(const QString &line) const {
    if (!problem.isEmpty()) return false;
    return root < 0 || evaluate(root, line);
}
QString LineFilter::select(const QString &text, int *matchedLines) const {
    QStringList result;
    const auto lines = text.split('\n');
    for (const auto &line : lines)
        if (matches(line)) result << line;
    if (matchedLines) *matchedLines = result.size();
    return result.join('\n');
}
