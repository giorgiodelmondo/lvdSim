// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sim/parsers/json_replies.hpp"

#include <QIcon>
#include <QPixmap>
#include <QString>

namespace lvd::gui {

/// Small vector icons for the kind of a signal in the Objects pane: input (arrow into a box), output (arrow out
/// of a box), inout (double arrow), buffer (arrow out of a bar), internal (diamond) and unknown (hollow ring,
/// neutral grey). Drawn with QPainter at whatever size and device pixel ratio the view asks for, so they are
/// sharp at 100% and 150% scale; the box outlines follow the text colour of the application palette, the arrows
/// keep their hue on a dark and on a light theme.
QIcon portModeIcon(PortMode mode);

/// The icon as a pixmap of `size` logical pixels (tests, legends in docs).
QPixmap portModePixmap(PortMode mode, int size, qreal devicePixelRatio = 1.0);

/// "input port", "output port", "inout port", "buffer port", "internal signal", "direction not known".
QString portModeText(PortMode mode);

} // namespace lvd::gui
