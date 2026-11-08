// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "wave/irenderer.hpp"

#include <QColor>

#include <cstdint>
#include <vector>

class QPainter;

namespace lvd::gui {

/// A colour with straight (not premultiplied) alpha.
struct Rgba {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;

    static Rgba from(const QColor& c) {
        return {static_cast<std::uint8_t>(c.red()), static_cast<std::uint8_t>(c.green()),
                static_cast<std::uint8_t>(c.blue()), static_cast<std::uint8_t>(c.alpha())};
    }
    QColor toQColor() const { return QColor(r, g, b, a); }
};

enum class PrimKind : std::uint8_t {
    Rect, ///< filled axis-aligned rectangle covering [x0, x1) x [y0, y1) in pixels
    Poly, ///< filled convex polygon, points[first .. first + 2 * count)
};

struct Prim {
    PrimKind kind = PrimKind::Rect;
    Rgba color;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    std::uint32_t first = 0, count = 0;
};

/// What a tile looks like, independent of how it is rasterised. PainterRenderer draws it with QPainter,
/// the GPU renderer turns it into triangles. Keeping the decisions (which levels, edges, envelopes, labels
/// of unknown values) here guarantees that both draw the same picture.
struct TileGeometry {
    int width = 0, height = 0;
    std::vector<Prim> prims;
    std::vector<float> points; ///< x, y pairs used by Poly primitives

    void rect(float x0, float y0, float x1, float y1, Rgba c) {
        prims.push_back({PrimKind::Rect, c, x0, y0, x1, y1, 0, 0});
    }
    /// The pixels from (x0, y) to (x1, y) inclusive, as a one pixel high rectangle.
    void hspan(int x0, int x1, int y, Rgba c) {
        rect(static_cast<float>(x0), static_cast<float>(y), static_cast<float>(x1 + 1),
             static_cast<float>(y + 1), c);
    }
    /// The pixels from (x, y0) to (x, y1) inclusive, as a one pixel wide rectangle.
    void vspan(int x, int y0, int y1, Rgba c) {
        rect(static_cast<float>(x), static_cast<float>(y0), static_cast<float>(x + 1),
             static_cast<float>(y1 + 1), c);
    }
    /// A one pixel wide line through the pixels that contain its end points (Bresenham), emitted as runs of
    /// pixels and clipped to the tile. Exact pixels instead of a rasteriser's line rules make every backend agree.
    void line(float x0, float y0, float x1, float y1, Rgba c);
    void poly(const std::vector<float>& xy, Rgba c);
};

/// Builds the picture of one tile. Costs O(tile width * log n) lookups, never iterates raw changes.
TileGeometry buildTileGeometry(const TileRequest& request, const Palette& palette);

/// Draws the geometry with QPainter (no antialiasing) onto whatever the painter paints on.
void paintGeometry(QPainter& painter, const TileGeometry& geometry);

} // namespace lvd::gui
