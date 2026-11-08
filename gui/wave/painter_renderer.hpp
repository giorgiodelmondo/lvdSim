// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "wave/irenderer.hpp"

namespace lvd::gui {

/// CPU renderer: builds the tile's geometry (wave/tile_geometry.hpp) and draws it with QPainter. Cost per
/// tile is bounded by the tile width: dense regions are resolved per pixel column with O(log n) lookups,
/// never by iterating raw changes.
class PainterRenderer final : public IRenderer {
public:
    explicit PainterRenderer(Palette palette = {})
        : palette_(std::move(palette)) {}

    QImage renderTile(const TileRequest& request) const override;
    const char* name() const noexcept override { return "QPainter"; }
    const Palette& palette() const noexcept { return palette_; }

private:
    Palette palette_;
};

} // namespace lvd::gui
