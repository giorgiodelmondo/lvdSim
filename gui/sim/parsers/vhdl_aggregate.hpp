// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QVector>

#include <optional>

namespace lvd::gui {

/// One element of a VHDL aggregate as the kernel's value printer writes it: `a => 7` (named, a record field)
/// or `7` (positional, an array element).
struct AggregateElement {
    QString name; ///< empty for a positional element
    QString value;
};

/// Splits a value in VHDL aggregate syntax, `(a => 7, b => '1')`, `(3, 4, 5)` or `((a => 1), (a => 2))`, into
/// its top level elements. Commas and arrows inside strings ("a,b"), character literals (',') and nested
/// parentheses do not split. Returns nullopt when the text is not one parenthesised aggregate (a scalar, a
/// string literal, unbalanced text) or is empty, `()`.
std::optional<QVector<AggregateElement>> splitAggregate(const QString& text);

} // namespace lvd::gui
