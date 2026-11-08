// SPDX-License-Identifier: GPL-3.0-or-later
#include "wave/painter_renderer.hpp"

#include "wave/tile_geometry.hpp"

#include <QPainter>

namespace lvd::gui {

QImage PainterRenderer::renderTile(const TileRequest& r) const {
    QImage img(r.widthPx, r.heightPx, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    const TileGeometry geometry = buildTileGeometry(r, palette_);
    if (geometry.prims.empty())
        return img;

    QPainter p(&img);
    paintGeometry(p, geometry);
    return img;
}

} // namespace lvd::gui
