// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSyntaxHighlighter>
#include <QTextCharFormat>

#include <vector>

namespace lvd::gui {

enum class VhdlToken { Keyword, Type, Comment, String, Character, Number, Attribute };

struct VhdlSpan {
    int start = 0;
    int length = 0;
    VhdlToken kind = VhdlToken::Keyword;
};

/// Splits one line of VHDL into coloured spans. Plain text (identifiers, operators) gets no span. `inBlock`
/// carries the state of a block comment (VHDL-2008) from one line to the next. Pure function, tested alone.
std::vector<VhdlSpan> tokenizeVhdlLine(const QString& line, bool& inBlock);

/// QSyntaxHighlighter on top of tokenizeVhdlLine. The colours follow the lightness of the palette.
class VhdlHighlighter : public QSyntaxHighlighter {
public:
    explicit VhdlHighlighter(QTextDocument* document);

protected:
    void highlightBlock(const QString& text) override;

private:
    QTextCharFormat formats_[7];
};

} // namespace lvd::gui
