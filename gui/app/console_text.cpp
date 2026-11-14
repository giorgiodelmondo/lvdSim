// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/console_text.hpp"

#include <QDir>
#include <QRegularExpression>

#include <algorithm>
#include <vector>

namespace lvd::gui {

bool tclCommandComplete(const QString& s) {
    enum class Kind { Script, Brace, Quote };
    struct Frame {
        Kind kind;
        int depth;    ///< open braces (Brace)
        bool bracket; ///< a [command] substitution (Script)
    };
    std::vector<Frame> stack{{Kind::Script, 0, false}};
    bool wordStart = true;
    bool commandStart = true;
    const qsizetype n = s.size();
    for (qsizetype i = 0; i < n; ++i) {
        const QChar c = s[i];
        if (c == QLatin1Char('\\')) {
            if (i + 1 >= n)
                return false; // a backslash at the very end continues the line
            ++i;
            wordStart = commandStart = false;
            continue;
        }
        switch (stack.back().kind) {
        case Kind::Brace:
            if (c == QLatin1Char('{')) {
                ++stack.back().depth;
            } else if (c == QLatin1Char('}') && --stack.back().depth == 0) {
                stack.pop_back();
                wordStart = commandStart = false;
            }
            break;
        case Kind::Quote:
            if (c == QLatin1Char('"')) {
                stack.pop_back();
                wordStart = commandStart = false;
            } else if (c == QLatin1Char('[')) {
                stack.push_back({Kind::Script, 0, true});
                wordStart = commandStart = true;
            }
            break;
        case Kind::Script:
            if (commandStart && c == QLatin1Char('#')) {
                // A comment runs to the end of the line; a backslash before the newline continues it.
                qsizetype j = i;
                while (j < n && s[j] != QLatin1Char('\n')) {
                    if (s[j] == QLatin1Char('\\')) {
                        if (j + 1 >= n)
                            return false;
                        ++j;
                    }
                    ++j;
                }
                i = j; // the newline (or the end); the loop steps over it
                wordStart = commandStart = true;
            } else if (c == QLatin1Char(' ') || c == QLatin1Char('\t') || c == QLatin1Char('\r')) {
                wordStart = true;
            } else if (c == QLatin1Char('\n') || c == QLatin1Char(';')) {
                wordStart = commandStart = true;
            } else if (c == QLatin1Char('{') && wordStart) {
                stack.push_back({Kind::Brace, 1, false});
                wordStart = commandStart = false;
            } else if (c == QLatin1Char('"') && wordStart) {
                stack.push_back({Kind::Quote, 0, false});
                wordStart = commandStart = false;
            } else if (c == QLatin1Char('[')) {
                stack.push_back({Kind::Script, 0, true});
                wordStart = commandStart = true;
            } else if (c == QLatin1Char(']') && stack.back().bracket) {
                stack.pop_back();
                wordStart = commandStart = false;
            } else {
                wordStart = commandStart = false;
            }
            break;
        }
    }
    return stack.size() == 1;
}

QStringList splitTopLevelCommands(const QString& script) {
    QStringList out;
    QString text = script;
    text.remove(QLatin1Char('\r'));
    QString group;
    bool open = false; // group holds lines of an unfinished command
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        group = open ? group + QLatin1Char('\n') + line : line;
        open = !tclCommandComplete(group);
        if (!open && !group.trimmed().isEmpty())
            out << group;
    }
    if (open && !group.trimmed().isEmpty())
        out << group;
    return out;
}

QString oneLineCommand(const QString& command) {
    QString c = command;
    c.remove(QLatin1Char('\r'));
    while (c.endsWith(QLatin1Char('\n')))
        c.chop(1);
    if (!c.contains(QLatin1Char('\n')))
        return c;
    QString out = QStringLiteral("eval \"");
    out.reserve(c.size() + c.size() / 8 + 8);
    for (const QChar ch : std::as_const(c)) {
        switch (ch.unicode()) {
        case '\\':
            out += QLatin1String("\\\\");
            break;
        case '"':
            out += QLatin1String("\\\"");
            break;
        case '$':
            out += QLatin1String("\\$");
            break;
        case '[':
            out += QLatin1String("\\[");
            break;
        case ']':
            out += QLatin1String("\\]");
            break;
        case '\n':
            out += QLatin1String("\\n");
            break;
        default:
            out += ch;
        }
    }
    out += QLatin1Char('"');
    return out;
}

namespace {

qsizetype promptLength(const QString& line) {
    qsizetype k = 0;
    while (line.mid(k, 2) == QLatin1String("% "))
        k += 2;
    if (k == 0 && line == QLatin1String("%"))
        return 1; // a prompt with nothing after it
    return k;
}

} // namespace

QString stripPrompts(const QString& text) {
    QStringList lines = text.split(QLatin1Char('\n'));
    for (QString& l : lines)
        l = l.mid(promptLength(l));
    return lines.join(QLatin1Char('\n'));
}

LineSeverity lineSeverity(const QString& line) {
    const QStringView l = QStringView(line).mid(promptLength(line));
    if (!l.startsWith(QLatin1String("** ")))
        return LineSeverity::None;
    const QStringView w = l.mid(3);
    if (w.startsWith(QLatin1String("Error")) || w.startsWith(QLatin1String("Fatal")) ||
        w.startsWith(QLatin1String("Failure")))
        return LineSeverity::Error;
    if (w.startsWith(QLatin1String("Warning")))
        return LineSeverity::Warning;
    if (w.startsWith(QLatin1String("Note")))
        return LineSeverity::Note;
    return LineSeverity::None;
}

QVector<SourceRef> findSourceRefs(const QString& line) {
    QVector<SourceRef> refs;
    // Cheap test first: most transcript lines have no "<something>.<ext>:<digit>" or Tcl trace at all.
    if (!line.contains(QLatin1Char(':')) && !line.contains(QLatin1String("(file \"")))
        return refs;
    static const QRegularExpression tclTrace(QStringLiteral("\\(file \"([^\"]+)\" line (\\d+)\\)"));
    static const QRegularExpression fileLine(
        QStringLiteral(
            "((?:[A-Za-z]:)?(?:[^\\s:\"'(){}<>\\[\\]|,;]*[/\\\\])?[^\\s:\"'(){}<>\\[\\]|,;/\\\\]+\\."
            "(?:vhdl?|vho|vhi|sv|svh|svi|v|vh|do|tcl|udo)):(\\d+)(?::\\d+)?(?!\\w)"),
        QRegularExpression::CaseInsensitiveOption);
    for (auto it = tclTrace.globalMatch(line); it.hasNext();) {
        const auto m = it.next();
        refs.push_back({static_cast<int>(m.capturedStart(1)), static_cast<int>(m.capturedLength(1)),
                        m.captured(1), m.captured(2).toInt()});
    }
    for (auto it = fileLine.globalMatch(line); it.hasNext();) {
        const auto m = it.next();
        const int ln = m.captured(2).toInt();
        if (ln <= 0)
            continue;
        refs.push_back(
            {static_cast<int>(m.capturedStart(0)), static_cast<int>(m.capturedLength(0)), m.captured(1), ln});
    }
    std::sort(refs.begin(), refs.end(),
              [](const SourceRef& a, const SourceRef& b) { return a.start < b.start; });
    return refs;
}

QString tclFileWord(const QString& path) {
    const QString p = QDir::fromNativeSeparators(path);
    static const QRegularExpression plain(QStringLiteral("^[A-Za-z0-9_./:+-]+$"));
    if (plain.match(p).hasMatch())
        return p;
    return QLatin1Char('{') + p + QLatin1Char('}');
}

} // namespace lvd::gui
