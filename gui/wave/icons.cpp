// SPDX-License-Identifier: GPL-3.0-or-later
#include "wave/icons.hpp"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <cmath>

namespace lvd::gui {

namespace {

const QColor kGreen(0x2e, 0xa0, 0x43);
const QColor kRed(0xd7, 0x3a, 0x49);
const QColor kAmber(0xe0, 0xa0, 0x10);

// Everything is designed on a 24 x 24 grid and scaled.
void draw(QPainter& p, Icon icon, const QColor& fg) {
    const QPen stroke(fg, 1.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(stroke);
    p.setBrush(Qt::NoBrush);

    auto filled = [&](const QColor& c) {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
    };

    switch (icon) {
    case Icon::Run:
        filled(kGreen);
        p.drawPolygon(QPolygonF({{6, 4}, {20, 12}, {6, 20}}));
        break;

    case Icon::RunAll:
        filled(kGreen);
        p.drawPolygon(QPolygonF({{2, 5}, {12, 12}, {2, 19}}));
        p.drawPolygon(QPolygonF({{12, 5}, {22, 12}, {12, 19}}));
        break;

    case Icon::Restart: {
        // Open circle with an arrow head at its end
        QPainterPath arc;
        arc.arcMoveTo(QRectF(4, 4, 16, 16), 60);
        arc.arcTo(QRectF(4, 4, 16, 16), 60, 270);
        p.drawPath(arc);
        filled(fg);
        p.drawPolygon(QPolygonF({{19.5, 3.5}, {20.5, 10.5}, {13.5, 8.5}}));
        break;
    }

    case Icon::Stop:
        filled(kRed);
        p.drawRoundedRect(QRectF(5, 5, 14, 14), 2, 2);
        break;

    case Icon::GoStart:
        p.drawLine(QPointF(5, 5), QPointF(5, 19));
        filled(fg);
        p.drawPolygon(QPolygonF({{19, 5}, {19, 19}, {8, 12}}));
        break;

    case Icon::GoEnd:
        p.drawLine(QPointF(19, 5), QPointF(19, 19));
        filled(fg);
        p.drawPolygon(QPolygonF({{5, 5}, {5, 19}, {16, 12}}));
        break;

    case Icon::PrevEdge: // a step with an arrow pointing back along it
        p.drawPolyline(QPolygonF({{21, 18}, {14, 18}, {14, 6}, {8, 6}}));
        p.drawPolyline(QPolygonF({{11, 3}, {7, 6}, {11, 9}}));
        break;

    case Icon::NextEdge:
        p.drawPolyline(QPolygonF({{3, 18}, {10, 18}, {10, 6}, {16, 6}}));
        p.drawPolyline(QPolygonF({{13, 3}, {17, 6}, {13, 9}}));
        break;

    case Icon::GoTo: // arrow to a bar: jump to a time
        p.drawLine(QPointF(3, 12), QPointF(15, 12));
        p.drawPolyline(QPolygonF({{11, 7}, {16, 12}, {11, 17}}));
        p.drawLine(QPointF(20, 5), QPointF(20, 19));
        break;

    case Icon::ZoomIn:
    case Icon::ZoomOut: {
        p.drawEllipse(QRectF(3.5, 3.5, 12, 12));
        p.drawLine(QPointF(14, 14), QPointF(21, 21));
        p.drawLine(QPointF(7, 9.5), QPointF(12, 9.5));
        if (icon == Icon::ZoomIn)
            p.drawLine(QPointF(9.5, 7), QPointF(9.5, 12));
        break;
    }

    case Icon::ZoomFit: // four corners pointing outwards
        p.drawPolyline(QPolygonF({{3, 9}, {3, 3}, {9, 3}}));
        p.drawPolyline(QPolygonF({{15, 3}, {21, 3}, {21, 9}}));
        p.drawPolyline(QPolygonF({{21, 15}, {21, 21}, {15, 21}}));
        p.drawPolyline(QPolygonF({{9, 21}, {3, 21}, {3, 15}}));
        break;

    case Icon::AddCursor:
    case Icon::RemoveCursor:
        p.setPen(QPen(kAmber, 1.9, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(9, 3), QPointF(9, 21));
        p.setPen(stroke);
        p.drawLine(QPointF(14, 8), QPointF(21, 8));
        if (icon == Icon::AddCursor)
            p.drawLine(QPointF(17.5, 4.5), QPointF(17.5, 11.5));
        break;

    case Icon::Divider:
        p.drawLine(QPointF(3, 12), QPointF(21, 12));
        p.drawLine(QPointF(3, 7), QPointF(3, 17));
        p.drawLine(QPointF(21, 7), QPointF(21, 17));
        break;

    case Icon::Follow: // chevrons running into the end of the data: stay at the end
        p.drawPolyline(QPolygonF({{3, 6}, {9, 12}, {3, 18}}));
        p.drawPolyline(QPolygonF({{9.5, 6}, {15.5, 12}, {9.5, 18}}));
        p.drawLine(QPointF(20, 4), QPointF(20, 20));
        break;

    case Icon::StepDelta: // the delta sign: one delta cycle
        p.drawPolygon(QPolygonF({{12, 4}, {3.5, 19.5}, {20.5, 19.5}}));
        break;

    case Icon::StepTime: // a clock: one time step
        p.drawEllipse(QRectF(3.5, 3.5, 17, 17));
        p.drawPolyline(QPolygonF({{12, 7}, {12, 12}, {16, 14.5}}));
        break;
    }
}

} // namespace

QPixmap renderIcon(Icon icon, int size, const QColor& fg) {
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(size / 24.0, size / 24.0);
    draw(p, icon, fg);
    return pix;
}

QIcon makeIcon(Icon icon, const QPalette& palette) {
    const QColor fg = palette.color(QPalette::Active, QPalette::ButtonText);
    QIcon result;
    for (int size : {16, 20, 24, 32, 48})
        result.addPixmap(renderIcon(icon, size, fg));
    return result;
}

QIcon makeIcon(Icon icon) {
    return makeIcon(icon, QApplication::palette());
}

} // namespace lvd::gui
