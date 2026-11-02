#pragma once
#include <QRegularExpression>
#include <QString>
#include <QVector>

class LineFilter {
public:
    bool compile(const QString &expression, Qt::CaseSensitivity sensitivity, QString &error);
    bool matches(const QString &line) const;
    QString select(const QString &text, int *matchedLines = nullptr) const;
private:
    enum class Kind { Text, Number, Not, And, Or };
    struct Node {
        Kind kind = Kind::Text;
        int left = -1, right = -1, numberIndex = 0;
        QVector<uint> pattern;
        QRegularExpression field;
        QString comparison;
        double value = 0;
    };
    QVector<Node> nodes;
    QString source, problem;
    qsizetype position = 0;
    int root = -1;
    Qt::CaseSensitivity caseSensitivity = Qt::CaseInsensitive;
    int parseOr(int depth);
    int parseAnd(int depth);
    int parseUnary(int depth);
    int parseAtom();
    int add(Node node);
    int fail(const QString &message);
    void spaces();
    bool take(const QString &token);
    bool evaluate(int index, const QString &line) const;
};
