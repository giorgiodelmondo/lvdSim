// SPDX-License-Identifier: GPL-3.0-or-later
#include "wave/tile_geometry.hpp"

#include <QPainter>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace lvd::gui {

using lvd::wave::kNoIndex;

namespace {

Time toTime(double t) {
    return t <= 0.0 ? Time{0} : static_cast<Time>(t);
}

enum class Level { High, Low, Unknown, HighZ };

Level levelOf(Logic v) {
    switch (v) {
    case Logic::L0:
    case Logic::L:
        return Level::Low;
    case Logic::L1:
    case Logic::H:
        return Level::High;
    case Logic::Z:
        return Level::HighZ;
    default:
        return Level::Unknown;
    }
}

constexpr int kMargin = 3;

/// Time at the left edge of pixel column x of the tile. Computed from the global column number so that two
/// tiles that share an edge agree on its time exactly.
double columnTime(const TileRequest& r, int x) {
    return (static_cast<double>(r.tileIndex) * r.widthPx + x) * r.timePerPixel;
}

/// Number of leading columns of the tile that start at or before the end of the data.
int visibleColumns(const TileRequest& r) {
    if (r.validEnd == std::numeric_limits<Time>::max())
        return r.widthPx;
    const double end = static_cast<double>(r.validEnd);
    int n = static_cast<int>(std::clamp(std::floor((end - columnTime(r, 0)) / r.timePerPixel) + 1.0, 0.0,
                                        static_cast<double>(r.widthPx)));
    while (n < r.widthPx && columnTime(r, n) <= end)
        ++n;
    while (n > 0 && columnTime(r, n - 1) > end)
        --n;
    return n;
}

Rgba withAlpha(const QColor& c, int alpha) {
    QColor x = c;
    x.setAlpha(alpha);
    return Rgba::from(x);
}

// ---------------------------------------------------------------------------------------------

void buildScalar(TileGeometry& g, const TileRequest& r, const Palette& pal) {
    const RowSource& src = *r.source;
    const int hiY = kMargin;
    const int loY = r.heightPx - kMargin - 1;
    const int midY = r.heightPx / 2;

    auto colourFor = [&](Level l) {
        switch (l) {
        case Level::High:
            return Rgba::from(r.style.hasColor ? r.style.color : pal.high);
        case Level::Low:
            return Rgba::from(r.style.hasColor ? r.style.color : pal.low);
        case Level::HighZ:
            return Rgba::from(pal.highZ);
        case Level::Unknown:
            break;
        }
        return Rgba::from(pal.unknown);
    };
    auto yFor = [&](Level l) {
        switch (l) {
        case Level::High:
            return hiY;
        case Level::Low:
            return loY;
        case Level::HighZ:
            return midY;
        case Level::Unknown:
            break;
        }
        return midY;
    };

    struct Run {
        bool active = false;
        Level level = Level::Low;
        int x0 = 0;
    } run;

    auto flush = [&](int xEnd) { // emits the run over [x0, xEnd]
        if (!run.active || xEnd < run.x0)
            return;
        if (run.level == Level::Unknown) {
            g.rect(static_cast<float>(run.x0), static_cast<float>(hiY), static_cast<float>(xEnd + 1),
                   static_cast<float>(loY + 1), withAlpha(pal.unknown, 70));
            g.hspan(run.x0, xEnd, hiY, colourFor(run.level));
            g.hspan(run.x0, xEnd, loY, colourFor(run.level));
        } else {
            g.hspan(run.x0, xEnd, yFor(run.level), colourFor(run.level));
        }
        run.active = false;
    };

    for (int x = 0; x < r.widthPx; ++x) {
        const Time a = toTime(columnTime(r, x));
        const Time b = std::max<Time>(a + 1, toTime(columnTime(r, x + 1)));
        const auto [lo, hi] = src.changesIn(a, b);

        if (hi == lo) {
            if (lo == 0) { // nothing has happened yet
                flush(x - 1);
                continue;
            }
            const Level l = levelOf(src.scalar(lo - 1));
            if (run.active && run.level != l)
                flush(x - 1);
            if (!run.active)
                run = {true, l, x};
            continue;
        }

        // At least one edge in this column.
        flush(x - 1);
        const std::size_t n = hi - lo;
        if (n >= 3) { // too dense to resolve: solid block
            g.vspan(x, hiY, loY, Rgba::from(pal.dense));
            run = {};
            continue;
        }

        int yMin = loY, yMax = hiY;
        auto include = [&](Level l) {
            const int y = yFor(l);
            yMin = std::min(yMin, y);
            yMax = std::max(yMax, y);
        };
        if (lo > 0)
            include(levelOf(src.scalar(lo - 1)));
        for (std::size_t i = lo; i < hi; ++i)
            include(levelOf(src.scalar(i)));
        const Level last = levelOf(src.scalar(hi - 1));
        g.vspan(x, std::min(yMin, yMax), std::max(yMin, yMax), colourFor(last));
        run = {true, last, x};
    }
    flush(r.widthPx - 1);
}

// ---------------------------------------------------------------------------------------------

void buildBus(TileGeometry& g, const TileRequest& r, const Palette& pal) {
    const RowSource& src = *r.source;
    const int hiY = kMargin;
    const int loY = r.heightPx - kMargin - 1;
    const double midY = (hiY + loY) / 2.0;
    const double tileStart = columnTime(r, 0);
    const double tileEnd = columnTime(r, r.widthPx);

    const Time a0 = toTime(tileStart);
    const Time a1 = std::max<Time>(a0 + 1, toTime(tileEnd));
    const auto [lo, hi] = src.changesIn(a0, a1);

    const QColor busColour = r.style.hasColor ? r.style.color : pal.bus;
    auto colourFor = [&](std::size_t change) {
        return Rgba::from(src.isUnknown(change) ? pal.unknown : busColour);
    };

    if (hi - lo > static_cast<std::size_t>(r.widthPx) * 4) {
        // Dense: per pixel column.
        for (int x = 0; x < r.widthPx; ++x) {
            const Time a = toTime(columnTime(r, x));
            const Time b = std::max<Time>(a + 1, toTime(columnTime(r, x + 1)));
            const auto [clo, chi] = src.changesIn(a, b);
            if (chi > clo) {
                g.vspan(x, hiY, loY, Rgba::from(pal.dense));
            } else if (clo > 0) {
                const Rgba c = colourFor(clo - 1);
                g.vspan(x, hiY, hiY, c);
                g.vspan(x, loY, loY, c);
            }
        }
        return;
    }

    // Segments: the value held at the start of the tile, then one per change in the tile.
    struct Seg {
        double t0, t1;
        std::size_t change;
    };
    std::vector<Seg> segs;
    const double inf = std::numeric_limits<double>::infinity();
    // The last value is held up to the end of the data, not beyond it.
    const double tail = r.validEnd == std::numeric_limits<Time>::max() ? inf : static_cast<double>(r.validEnd);
    const std::size_t total = src.count();
    if (lo > 0)
        segs.push_back({-inf, lo < total ? static_cast<double>(src.time(lo)) : tail, lo - 1});
    for (std::size_t i = lo; i < hi; ++i) {
        const double t1 = i + 1 < total ? static_cast<double>(src.time(i + 1)) : tail;
        segs.push_back({static_cast<double>(src.time(i)), t1, i});
    }

    for (const Seg& s : segs) {
        const double x0 = std::isinf(s.t0) ? -4.0 : (s.t0 - tileStart) / r.timePerPixel;
        const double x1 = std::isinf(s.t1) ? r.widthPx + 4.0 : (s.t1 - tileStart) / r.timePerPixel;
        const Rgba c = colourFor(s.change);
        const double w = x1 - x0;

        if (w < 2.0) { // too narrow for an outline
            g.rect(static_cast<float>(x0), static_cast<float>(hiY), static_cast<float>(x0 + std::max(1.0, w)),
                   static_cast<float>(loY + 1), Rgba::from(pal.dense));
            continue;
        }
        const double slant = std::min(2.5, w / 2.0);
        const std::vector<float> pts = {
            static_cast<float>(x0),        static_cast<float>(midY),       static_cast<float>(x0 + slant),
            static_cast<float>(hiY + 0.5), static_cast<float>(x1 - slant), static_cast<float>(hiY + 0.5),
            static_cast<float>(x1),        static_cast<float>(midY),       static_cast<float>(x1 - slant),
            static_cast<float>(loY + 0.5), static_cast<float>(x0 + slant), static_cast<float>(loY + 0.5),
        };
        if (src.isUnknown(s.change)) {
            Rgba fill = c;
            fill.a = 60;
            g.poly(pts, fill);
        }
        for (std::size_t i = 0; i < 6; ++i) { // the outline
            const std::size_t j = (i + 1) % 6;
            g.line(pts[2 * i], pts[2 * i + 1], pts[2 * j], pts[2 * j + 1], c);
        }
    }
}

// ---------------------------------------------------------------------------------------------

void buildAnalog(TileGeometry& g, const TileRequest& r, const Palette& pal) {
    const RowSource& src = *r.source;
    const lvd::wave::NumericIndex* idx = src.numeric();
    if (idx == nullptr || !idx->valid() || idx->size() == 0)
        return buildBus(g, r, pal);

    // One range for every tile of the row: the whole signal (auto) or the user's limits (manual).
    std::optional<lvd::wave::AnalogRange> data;
    if (r.style.autoRange) {
        const lvd::wave::MinMax all = idx->range(0, idx->size());
        if (!all.empty())
            data = lvd::wave::AnalogRange{all.min, all.max};
    }
    lvd::wave::AnalogSettings settings;
    settings.autoRange = r.style.autoRange;
    settings.min = r.style.min;
    settings.max = r.style.max;
    const std::optional<lvd::wave::AnalogRange> range = lvd::wave::resolveAnalogRange(settings, data);
    if (!range)
        return;
    const double top = kMargin, bottom = r.heightPx - kMargin - 1;
    auto yOf = [&](double v) { return bottom - lvd::wave::analogFraction(*range, v) * (bottom - top); };

    // A live source may have more changes than the index has seen so far: never read past the index.
    const std::size_t n = std::min(src.count(), idx->size());
    const Rgba colour = Rgba::from(r.style.hasColor ? r.style.color : pal.analog);
    const int columns = visibleColumns(r);
    const double nan = std::numeric_limits<double>::quiet_NaN();

    // Linear traces: the interpolated value at time t (NaN before the first change), held after the last change.
    auto valueAt = [&](double t) {
        const std::size_t h = src.holdIndex(toTime(t));
        if (h == kNoIndex || h >= n)
            return nan;
        const double v = idx->value(h);
        if (h + 1 >= n || std::isnan(v))
            return v;
        const double v1 = idx->value(h + 1);
        if (std::isnan(v1))
            return v;
        const double t0 = static_cast<double>(src.time(h)), t1 = static_cast<double>(src.time(h + 1));
        return t1 > t0 ? v + (v1 - v) * std::clamp((t - t0) / (t1 - t0), 0.0, 1.0) : v;
    };

    // One pixel column at a time, whatever the zoom: the vertical extent of the trace over the column's time
    // span. Step: the value held into the column (the last change before it), then the values of the changes
    // inside it. Linear: the same changes plus the interpolated values at the left and right edge. Neighbouring
    // columns share an edge, so the trace is connected, a zoomed out picture is the envelope of a zoomed in
    // one, and tile edges are not special.
    for (int x = 0; x < columns; ++x) {
        const double tA = columnTime(r, x), tB = columnTime(r, x + 1);
        const Time a = toTime(tA);
        const Time b = std::max<Time>(a + 1, toTime(tB));
        auto [clo, chi] = src.changesIn(a, b);
        clo = std::min(clo, n);
        chi = std::min(chi, n);
        lvd::wave::MinMax m = idx->range(clo, chi);
        if (r.style.linear) {
            m.add(valueAt(tA));
            m.add(valueAt(tB));
        } else if (clo > 0) {
            m.add(idx->value(clo - 1));
        }
        if (m.empty())
            continue;
        const int y0 = static_cast<int>(std::lround(yOf(m.max)));
        const int y1 = static_cast<int>(std::lround(yOf(m.min)));
        g.vspan(x, y0, std::max(y0, y1), colour);
    }
}

} // namespace

void TileGeometry::line(float fx0, float fy0, float fx1, float fy1, Rgba c) {
    int x0 = static_cast<int>(std::floor(fx0)), y0 = static_cast<int>(std::floor(fy0));
    const int x1 = static_cast<int>(std::floor(fx1)), y1 = static_cast<int>(std::floor(fy1));
    const int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
    if (dx > 100000 || dy > 100000) return;  // far outside any tile

    auto putBox = [&](int ax, int ay, int bx, int by) {  // inclusive pixel box, clipped to the tile
        ax = std::max(ax, 0);
        ay = std::max(ay, 0);
        bx = std::min(bx, width - 1);
        by = std::min(by, height - 1);
        if (ax <= bx && ay <= by)
            rect(static_cast<float>(ax), static_cast<float>(ay), static_cast<float>(bx + 1), static_cast<float>(by + 1), c);
    };

    if (dx == 0) {  // vertical
        putBox(x0, std::min(y0, y1), x0, std::max(y0, y1));
        return;
    }

    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    int runY = y0, runA = x0, runB = x0;  // the current horizontal run [runA, runB] on row runY
    for (;;) {
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        int nx = x0, ny = y0;
        if (e2 > -dy) {
            err -= dy;
            nx += sx;
        }
        if (e2 < dx) {
            err += dx;
            ny += sy;
        }
        if (ny == runY) {
            runA = std::min(runA, nx);
            runB = std::max(runB, nx);
        } else {
            putBox(runA, runY, runB, runY);
            runY = ny;
            runA = runB = nx;
        }
        x0 = nx;
        y0 = ny;
    }
    putBox(runA, runY, runB, runY);
}

void TileGeometry::poly(const std::vector<float>& xy, Rgba c) {
    Prim p;
    p.kind = PrimKind::Poly;
    p.color = c;
    p.first = static_cast<std::uint32_t>(points.size());
    p.count = static_cast<std::uint32_t>(xy.size() / 2);
    points.insert(points.end(), xy.begin(), xy.end());
    prims.push_back(p);
}

namespace {

/// Nothing is drawn for times after the end of the recorded data: drop every rectangle that starts in a column
/// after the one holding the end and trim the others.
void clipToValidEnd(TileGeometry& g, const TileRequest& r) {
    const int columns = visibleColumns(r);
    if (columns >= r.widthPx)
        return;
    const float limit = static_cast<float>(columns);
    std::size_t keep = 0;
    for (const Prim& p : g.prims) {
        Prim q = p;
        if (q.kind == PrimKind::Rect) {
            if (q.x0 >= limit)
                continue;
            q.x1 = std::min(q.x1, limit);
        }
        g.prims[keep++] = q;
    }
    g.prims.resize(keep);
}

} // namespace

TileGeometry buildTileGeometry(const TileRequest& r, const Palette& palette) {
    TileGeometry g;
    g.width = r.widthPx;
    g.height = r.heightPx;
    if (r.source == nullptr || r.source->count() == 0)
        return g;

    switch (r.source->waveClass()) {
    case WaveClass::Scalar:
        buildScalar(g, r, palette);
        break;
    case WaveClass::Bus:
        buildBus(g, r, palette);
        break;
    case WaveClass::Analog:
        buildAnalog(g, r, palette);
        break;
    }
    clipToValidEnd(g, r);
    return g;
}

void paintGeometry(QPainter& p, const TileGeometry& g) {
    p.setRenderHint(QPainter::Antialiasing, false);
    for (const Prim& prim : g.prims) {
        const QColor c = prim.color.toQColor();
        switch (prim.kind) {
        case PrimKind::Rect:
            p.fillRect(QRectF(prim.x0, prim.y0, prim.x1 - prim.x0, prim.y1 - prim.y0), c);
            break;
        case PrimKind::Poly: {
            QPolygonF poly;
            for (std::uint32_t i = 0; i < prim.count; ++i)
                poly << QPointF(g.points[prim.first + 2 * i], g.points[prim.first + 2 * i + 1]);
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawPolygon(poly);
            p.setBrush(Qt::NoBrush);
            break;
        }
        }
    }
}

} // namespace lvd::gui
