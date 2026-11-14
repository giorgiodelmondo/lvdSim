// SPDX-License-Identifier: GPL-3.0-or-later
#include "sim/parsers/vhdl_aggregate.hpp"

namespace lvd::gui {

namespace {

/// Index just after the literal that starts at `i` ("..." with "" inside, or 'c'), or i + 1 for any other
/// character.
qsizetype skipLiteral(const QString& t, qsizetype i) {
    const QChar c = t[i];
    if (c == QLatin1Char('"')) {
        qsizetype j = i + 1;
        while (j < t.size()) {
            if (t[j] == QLatin1Char('"')) {
                if (j + 1 < t.size() && t[j + 1] == QLatin1Char('"')) {
                    j += 2;
                    continue;
                }
                return j + 1;
            }
            ++j;
        }
        return -1; // unterminated
    }
    if (c == QLatin1Char('\'') && i + 2 < t.size() && t[i + 2] == QLatin1Char('\''))
        return i + 3;
    return i + 1;
}

AggregateElement element(const QString& part) {
    // A top level "=>" (outside literals and parentheses) makes it a named element.
    int depth = 0;
    for (qsizetype i = 0; i < part.size();) {
        const QChar c = part[i];
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            const qsizetype next = skipLiteral(part, i);
            if (next < 0)
                break;
            i = next;
            continue;
        }
        if (c == QLatin1Char('('))
            ++depth;
        else if (c == QLatin1Char(')'))
            --depth;
        else if (depth == 0 && c == QLatin1Char('=') && i + 1 < part.size() &&
                 part[i + 1] == QLatin1Char('>'))
            return {part.left(i).trimmed(), part.mid(i + 2).trimmed()};
        ++i;
    }
    return {QString(), part.trimmed()};
}

} // namespace

std::optional<QVector<AggregateElement>> splitAggregate(const QString& text) {
    const QString t = text.trimmed();
    if (t.size() < 3 || t.front() != QLatin1Char('(') || t.back() != QLatin1Char(')'))
        return std::nullopt;
    QVector<AggregateElement> out;
    int depth = 0;
    qsizetype start = 1;
    for (qsizetype i = 0; i < t.size();) {
        const QChar c = t[i];
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            const qsizetype next = skipLiteral(t, i);
            if (next < 0)
                return std::nullopt;
            i = next;
            continue;
        }
        if (c == QLatin1Char('(')) {
            ++depth;
        } else if (c == QLatin1Char(')')) {
            --depth;
            if (depth == 0 && i != t.size() - 1)
                return std::nullopt; // "(a) (b)": not one aggregate
            if (depth < 0)
                return std::nullopt;
        } else if (c == QLatin1Char(',') && depth == 1) {
            out.push_back(element(t.mid(start, i - start)));
            start = i + 1;
        }
        ++i;
    }
    if (depth != 0)
        return std::nullopt;
    out.push_back(element(t.mid(start, t.size() - 1 - start)));
    for (const AggregateElement& e : out)
        if (e.value.isEmpty())
            return std::nullopt;
    return out;
}

} // namespace lvd::gui
