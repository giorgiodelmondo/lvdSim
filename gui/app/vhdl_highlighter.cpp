// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/vhdl_highlighter.hpp"

#include <QApplication>
#include <QPalette>
#include <QSet>

namespace lvd::gui {

namespace {

const QSet<QString>& keywords() {
    static const QSet<QString> k = {"abs",
                                    "access",
                                    "after",
                                    "alias",
                                    "all",
                                    "and",
                                    "architecture",
                                    "array",
                                    "assert",
                                    "assume",
                                    "attribute",
                                    "begin",
                                    "block",
                                    "body",
                                    "buffer",
                                    "bus",
                                    "case",
                                    "component",
                                    "configuration",
                                    "constant",
                                    "context",
                                    "cover",
                                    "default",
                                    "disconnect",
                                    "downto",
                                    "else",
                                    "elsif",
                                    "end",
                                    "entity",
                                    "exit",
                                    "fairness",
                                    "file",
                                    "for",
                                    "force",
                                    "function",
                                    "generate",
                                    "generic",
                                    "group",
                                    "guarded",
                                    "if",
                                    "impure",
                                    "in",
                                    "inertial",
                                    "inout",
                                    "is",
                                    "label",
                                    "library",
                                    "linkage",
                                    "literal",
                                    "loop",
                                    "map",
                                    "mod",
                                    "nand",
                                    "new",
                                    "next",
                                    "nor",
                                    "not",
                                    "null",
                                    "of",
                                    "on",
                                    "open",
                                    "or",
                                    "others",
                                    "out",
                                    "package",
                                    "parameter",
                                    "port",
                                    "postponed",
                                    "procedure",
                                    "process",
                                    "property",
                                    "protected",
                                    "pure",
                                    "range",
                                    "record",
                                    "register",
                                    "reject",
                                    "release",
                                    "rem",
                                    "report",
                                    "restrict",
                                    "return",
                                    "rol",
                                    "ror",
                                    "select",
                                    "sequence",
                                    "severity",
                                    "shared",
                                    "signal",
                                    "sla",
                                    "sll",
                                    "sra",
                                    "srl",
                                    "strong",
                                    "subtype",
                                    "then",
                                    "to",
                                    "transport",
                                    "type",
                                    "unaffected",
                                    "units",
                                    "until",
                                    "use",
                                    "variable",
                                    "vmode",
                                    "vprop",
                                    "vunit",
                                    "wait",
                                    "when",
                                    "while",
                                    "with",
                                    "xnor",
                                    "xor"};
    return k;
}

const QSet<QString>& types() {
    static const QSet<QString> t = {"bit",
                                    "bit_vector",
                                    "boolean",
                                    "character",
                                    "integer",
                                    "natural",
                                    "positive",
                                    "real",
                                    "severity_level",
                                    "std_logic",
                                    "std_logic_vector",
                                    "std_ulogic",
                                    "std_ulogic_vector",
                                    "string",
                                    "time",
                                    "signed",
                                    "unsigned",
                                    "line",
                                    "text",
                                    "file_open_status",
                                    "delay_length",
                                    "boolean_vector",
                                    "integer_vector",
                                    "real_vector",
                                    "time_vector"};
    return t;
}

bool isIdentChar(QChar c) {
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

bool isBitStringBase(const QString& w) {
    static const QSet<QString> b = {"b", "o", "x", "d", "ub", "uo", "ux", "sb", "so", "sx"};
    return b.contains(w.toLower());
}

} // namespace

std::vector<VhdlSpan> tokenizeVhdlLine(const QString& line, bool& inBlock) {
    std::vector<VhdlSpan> spans;
    const int n = static_cast<int>(line.size());
    int i = 0;

    auto add = [&](int start, int end, VhdlToken kind) { spans.push_back({start, end - start, kind}); };
    const QLatin1Char tick('\'');
    const QLatin1Char dquote('"');

    if (inBlock) {
        const int end = static_cast<int>(line.indexOf(QLatin1String("*/")));
        if (end < 0) {
            add(0, n, VhdlToken::Comment);
            return spans;
        }
        add(0, end + 2, VhdlToken::Comment);
        inBlock = false;
        i = end + 2;
    }

    while (i < n) {
        const QChar c = line[i];
        if (c == QLatin1Char('-') && i + 1 < n && line[i + 1] == QLatin1Char('-')) {
            add(i, n, VhdlToken::Comment);
            return spans;
        }
        if (c == QLatin1Char('/') && i + 1 < n && line[i + 1] == QLatin1Char('*')) {
            const int end = static_cast<int>(line.indexOf(QLatin1String("*/"), i + 2));
            if (end < 0) {
                add(i, n, VhdlToken::Comment);
                inBlock = true;
                return spans;
            }
            add(i, end + 2, VhdlToken::Comment);
            i = end + 2;
            continue;
        }
        if (c == dquote) {
            int j = i + 1;
            while (j < n) {
                if (line[j] == dquote) {
                    if (j + 1 < n && line[j + 1] == dquote) { // doubled quote inside the string
                        j += 2;
                        continue;
                    }
                    ++j;
                    break;
                }
                ++j;
            }
            add(i, j, VhdlToken::String);
            i = j;
            continue;
        }
        if (c == tick) {
            const QChar prev = i > 0 ? line[i - 1] : QChar(' ');
            const bool afterName = isIdentChar(prev) || prev == QLatin1Char(')');
            if (i + 2 < n && line[i + 2] == tick && !afterName) {
                add(i, i + 3, VhdlToken::Character);
                i += 3;
                continue;
            }
            int j = i + 1;
            while (j < n && isIdentChar(line[j]))
                ++j;
            if (j > i + 1 && afterName)
                add(i, j, VhdlToken::Attribute); // clk'event
            i = j > i + 1 ? j : i + 1;
            continue;
        }
        if (c == QLatin1Char('\\')) { // extended identifier
            const int end = static_cast<int>(line.indexOf(QLatin1Char('\\'), i + 1));
            i = end < 0 ? n : end + 1;
            continue;
        }
        if (c.isDigit()) {
            int j = i;
            while (j < n && (line[j].isDigit() || line[j] == QLatin1Char('_')))
                ++j;
            if (j < n && line[j] == QLatin1Char('#')) { // based literal 16#FF#
                const int close = static_cast<int>(line.indexOf(QLatin1Char('#'), j + 1));
                j = close < 0 ? n : close + 1;
            } else {
                if (j + 1 < n && line[j] == QLatin1Char('.') && line[j + 1].isDigit()) {
                    ++j;
                    while (j < n && (line[j].isDigit() || line[j] == QLatin1Char('_')))
                        ++j;
                }
                if (j < n && (line[j] == QLatin1Char('e') || line[j] == QLatin1Char('E'))) {
                    int k = j + 1;
                    if (k < n && (line[k] == QLatin1Char('+') || line[k] == QLatin1Char('-')))
                        ++k;
                    if (k < n && line[k].isDigit()) {
                        while (k < n && line[k].isDigit())
                            ++k;
                        j = k;
                    }
                }
            }
            add(i, j, VhdlToken::Number);
            i = j;
            continue;
        }
        if (c.isLetter()) {
            int j = i + 1;
            while (j < n && isIdentChar(line[j]))
                ++j;
            const QString word = line.mid(i, j - i);
            if (j < n && line[j] == dquote && isBitStringBase(word)) { // x"FF"
                int e = j + 1;
                while (e < n && line[e] != dquote)
                    ++e;
                e = e < n ? e + 1 : n;
                add(i, e, VhdlToken::String);
                i = e;
                continue;
            }
            const QString lower = word.toLower();
            if (keywords().contains(lower))
                add(i, j, VhdlToken::Keyword);
            else if (types().contains(lower))
                add(i, j, VhdlToken::Type);
            i = j;
            continue;
        }
        ++i;
    }
    return spans;
}

VhdlHighlighter::VhdlHighlighter(QTextDocument* document)
    : QSyntaxHighlighter(document) {
    const bool dark = QApplication::palette().color(QPalette::Base).lightness() < 128;
    auto fmt = [](const QColor& c, bool bold = false, bool italic = false) {
        QTextCharFormat f;
        f.setForeground(c);
        if (bold)
            f.setFontWeight(QFont::Bold);
        f.setFontItalic(italic);
        return f;
    };
    auto set = [this](VhdlToken t, const QTextCharFormat& f) { formats_[static_cast<int>(t)] = f; };
    set(VhdlToken::Keyword, fmt(dark ? QColor(0x56, 0x9c, 0xd6) : QColor(0x00, 0x3f, 0xa8), true));
    set(VhdlToken::Type, fmt(dark ? QColor(0x4e, 0xc9, 0xb0) : QColor(0x00, 0x7a, 0x6a)));
    set(VhdlToken::Comment, fmt(dark ? QColor(0x6a, 0x99, 0x55) : QColor(0x4a, 0x7a, 0x32), false, true));
    set(VhdlToken::String, fmt(dark ? QColor(0xce, 0x91, 0x78) : QColor(0xa3, 0x1b, 0x1b)));
    set(VhdlToken::Character, formats_[static_cast<int>(VhdlToken::String)]);
    set(VhdlToken::Number, fmt(dark ? QColor(0xb5, 0xce, 0xa8) : QColor(0x9a, 0x4a, 0x00)));
    set(VhdlToken::Attribute, fmt(dark ? QColor(0xdc, 0xdc, 0xaa) : QColor(0x70, 0x40, 0x90)));
}

void VhdlHighlighter::highlightBlock(const QString& text) {
    bool inBlock = previousBlockState() == 1;
    for (const VhdlSpan& s : tokenizeVhdlLine(text, inBlock))
        setFormat(s.start, s.length, formats_[static_cast<int>(s.kind)]);
    setCurrentBlockState(inBlock ? 1 : 0);
}

} // namespace lvd::gui
