// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPalette>

class QApplication;

namespace lvd::gui {

/// Contrast ratio of two colours (WCAG relative luminance), 1 (equal) to 21 (black on white).
double contrastRatio(const QColor& a, const QColor& b);

/// Placeholder text (also the dim text of the Source line numbers and the Coverage dock) must stay readable
/// on the text background. Some desktop themes hand Qt a dark palette whose placeholder colour is black
/// (seen with XFCE and the GTK platform theme after the window is shown), which makes the hints of the
/// Go to, Filter, Find and console fields invisible. Every colour group whose placeholder has less than
/// kMinPlaceholderContrast against Base gets Qt's own default instead: Text half way to Base.
/// Returns true when the palette was changed.
constexpr double kMinPlaceholderContrast = 3.0;
bool repairPalette(QPalette& palette);

/// Repairs the application palette now and again after every palette change of the desktop theme.
void installPaletteRepair(QApplication& app);

} // namespace lvd::gui
