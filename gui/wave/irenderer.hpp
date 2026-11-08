// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "wave/row_source.hpp"

#include <QColor>
#include <QImage>

#include <limits>

namespace lvd::gui {

/// Colours of the wave area. Defaults are a dark scheme with green traces.
struct Palette {
    QColor background{0x10, 0x10, 0x10};
    QColor grid{0x30, 0x30, 0x30};
    QColor high{0x40, 0xe0, 0x40};
    QColor low{0x40, 0xe0, 0x40};
    QColor bus{0x40, 0xd0, 0xe0};
    QColor unknown{0xff, 0x40, 0x40};
    QColor highZ{0x50, 0x90, 0xff};
    QColor dense{0xd0, 0xd0, 0x60};
    QColor analog{0xff, 0xc0, 0x40};
    QColor text{0xe0, 0xe0, 0xe0};
    QColor cursor{0xff, 0xe0, 0x40};
    QColor cursor2{0xff, 0x80, 0xff};
    QColor selection{0x30, 0x50, 0x90};
};

/// Per row drawing options that influence tile contents.
struct RowStyle {
    bool autoRange = true;
    double min = 0.0;
    double max = 1.0;
    bool linear = false;
    bool hasColor = false;  ///< user chosen colour: replaces the palette colour of levels, outlines, analog
    QColor color;
};

/// One tile: a fixed width slice of one row at one zoom level. Tile k covers the times
/// [k * widthPx * timePerPixel, (k + 1) * widthPx * timePerPixel).
struct TileRequest {
    const RowSource* source = nullptr;
    RowStyle style;
    double timePerPixel = 1.0;
    long long tileIndex = 0;
    int widthPx = 256;
    int heightPx = 22;
    /// The end of the recorded data (the simulation time reached). Nothing is drawn after it. The default,
    /// the largest time, means there is no end: the last value is held for ever.
    Time validEnd = std::numeric_limits<Time>::max();
};

/// Draws wave row tiles. Tiles contain only shapes on a transparent background: grid, text, cursors and
/// names are drawn by the widget on top, so tiles stay valid while those change.
/// Implementations: PainterRenderer (CPU, always available) and, later, a QRhi accelerated one.
class IRenderer {
public:
    static constexpr int kTileWidth = 256;

    virtual ~IRenderer() = default;
    virtual QImage renderTile(const TileRequest& request) const = 0;
    virtual const char* name() const noexcept = 0;
};

} // namespace lvd::gui
