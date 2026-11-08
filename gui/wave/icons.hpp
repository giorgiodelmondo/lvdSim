// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QIcon>
#include <QPalette>
#include <QPixmap>

namespace lvd::gui {

/// Toolbar icons, drawn with QPainter so they need no image files or fonts and stay sharp at any size.
enum class Icon {
    Run,
    RunAll,
    Restart,
    Stop,
    GoStart,
    PrevEdge,
    NextEdge,
    GoEnd,
    GoTo,
    ZoomIn,
    ZoomOut,
    ZoomFit,
    AddCursor,
    RemoveCursor,
    Divider,
    StepDelta,
    StepTime,
    Follow,
};

inline constexpr int kIconCount = 18;

/// One icon at a pixel size. `fg` is used for the outline shapes; Run, Stop and the cursor icons keep their
/// own accent colour (green, red, amber) so they are recognised at a glance.
QPixmap renderIcon(Icon icon, int size, const QColor& fg);

/// A QIcon with several sizes (16 to 48 pixels), using the text colour of the given palette. Qt derives the
/// greyed out look of disabled actions from it.
QIcon makeIcon(Icon icon, const QPalette& palette);
QIcon makeIcon(Icon icon); ///< with the application palette

} // namespace lvd::gui
