// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/port_icons.hpp"

#include <QApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace lvd::gui {

namespace {

// Designed on a 16 x 16 grid.
QColor accentFor(PortMode m) {
    switch (m) {
    case PortMode::In:
        return QColor(0x2e, 0xa0, 0x5a); // green
    case PortMode::Out:
        return QColor(0xe0, 0x7b, 0x2c); // orange
    case PortMode::InOut:
        return QColor(0x3d, 0x8e, 0xe6); // blue
    case PortMode::Buffer:
        return QColor(0xd2, 0x4a, 0x6a); // crimson
    case PortMode::Internal:
    case PortMode::Unknown:
        break;
    }
    return QColor(0x8c, 0x96, 0xa3); // neutral grey
}

void head(QPainter& p, double tipX, double y, double dir) {
    p.drawPolyline(QPolygonF({{tipX - 3.2 * dir, y - 3.0}, {tipX, y}, {tipX - 3.2 * dir, y + 3.0}}));
}

// On a selected row the hues would fight the highlight (a blue inout arrow on a blue selection): draw the icon
// in the highlighted text colour then; the shapes still tell the kinds apart.
void drawMode(QPainter& p, PortMode mode, const QColor& outline, bool enabled, bool selected = false) {
    QColor accent = selected ? outline : accentFor(mode);
    QColor line = outline;
    if (!enabled) {
        accent.setAlphaF(0.45);
        line.setAlphaF(0.45);
    }
    const QPen arrow(accent, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    const QPen box(line, 1.3, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
    p.setBrush(Qt::NoBrush);

    switch (mode) {
    case PortMode::In: // arrow running into a box on the right
        p.setPen(box);
        p.drawRect(QRectF(10.0, 3.5, 4.5, 9.0));
        p.setPen(arrow);
        p.drawLine(QPointF(1.5, 8.0), QPointF(9.0, 8.0));
        head(p, 9.0, 8.0, 1.0);
        break;
    case PortMode::Out: // arrow leaving a box on the left
        p.setPen(box);
        p.drawRect(QRectF(1.5, 3.5, 4.5, 9.0));
        p.setPen(arrow);
        p.drawLine(QPointF(7.0, 8.0), QPointF(14.5, 8.0));
        head(p, 14.5, 8.0, 1.0);
        break;
    case PortMode::InOut:
        p.setPen(arrow);
        p.drawLine(QPointF(2.0, 8.0), QPointF(14.0, 8.0));
        head(p, 14.0, 8.0, 1.0);
        head(p, 2.0, 8.0, -1.0);
        break;
    case PortMode::Buffer: // an output that is also read back: arrow leaving a solid bar
        p.setPen(QPen(line, 2.2, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(QPointF(2.5, 3.0), QPointF(2.5, 13.0));
        p.setPen(arrow);
        p.drawLine(QPointF(5.0, 8.0), QPointF(14.5, 8.0));
        head(p, 14.5, 8.0, 1.0);
        break;
    case PortMode::Internal: { // a diamond: a node of the design
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawPolygon(QPolygonF({{8.0, 3.0}, {13.0, 8.0}, {8.0, 13.0}, {3.0, 8.0}}));
        break;
    }
    case PortMode::Unknown: // a hollow ring: nothing is claimed
        p.setPen(QPen(accent, 1.4));
        p.drawEllipse(QRectF(4.0, 4.0, 8.0, 8.0));
        break;
    }
}

class PortIconEngine : public QIconEngine {
public:
    explicit PortIconEngine(PortMode mode)
        : mode_(mode) {}

    QIconEngine* clone() const override { return new PortIconEngine(mode_); }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const double s = std::min(rect.width(), rect.height()) / 16.0;
        painter->translate(rect.x() + (rect.width() - 16.0 * s) / 2.0, rect.y() + (rect.height() - 16.0 * s) / 2.0);
        painter->scale(s, s);
        const QPalette pal = QApplication::palette();
        const bool selected = mode == QIcon::Selected;
        drawMode(*painter, mode_, pal.color(selected ? QPalette::HighlightedText : QPalette::Text),
                 mode != QIcon::Disabled, selected);
        painter->restore();
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap pm(size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        paint(&p, QRect(QPoint(0, 0), size), mode, state);
        return pm;
    }

private:
    PortMode mode_;
};

} // namespace

QIcon portModeIcon(PortMode mode) {
    return QIcon(new PortIconEngine(mode));
}

QPixmap portModePixmap(PortMode mode, int size, qreal dpr) {
    const int px = std::max(1, static_cast<int>(std::lround(size * dpr)));
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    pm.setDevicePixelRatio(dpr);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(size / 16.0, size / 16.0);
    drawMode(p, mode, QApplication::palette().color(QPalette::Text), true);
    return pm;
}

QString portModeText(PortMode mode) {
    switch (mode) {
    case PortMode::In:
        return QObject::tr("input port");
    case PortMode::Out:
        return QObject::tr("output port");
    case PortMode::InOut:
        return QObject::tr("inout port");
    case PortMode::Buffer:
        return QObject::tr("buffer port");
    case PortMode::Internal:
        return QObject::tr("internal signal");
    case PortMode::Unknown:
        break;
    }
    return QObject::tr("direction not known");
}

} // namespace lvd::gui
