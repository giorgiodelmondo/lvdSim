// SPDX-License-Identifier: GPL-3.0-or-later
#include "wave/wave_toolbar.hpp"

#include "wave/icons.hpp"
#include "wave/view_state.hpp"
#include "wave/wave_widget.hpp"

#include <QAction>
#include <QLabel>
#include <QLineEdit>

namespace lvd::gui {

QAction* WaveToolBar::add(const QString& objectName, const QString& text, const QString& tip,
                          const QIcon& icon) {
    QAction* a = addAction(icon, text);
    a->setObjectName(objectName);
    a->setToolTip(tip);
    return a;
}

WaveToolBar::WaveToolBar(WaveWidget* wave, QWidget* parent)
    : QToolBar(tr("Wave"), parent)
    , wave_(wave) {
    setObjectName("waveToolBar");
    setMovable(false);
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setIconSize(QSize(20, 20));

    connect(add("goStart", tr("Start"), tr("Go to the beginning of the simulation (Home)"),
                makeIcon(Icon::GoStart)),
            &QAction::triggered, this, [this] { wave_->goToStart(); });
    connect(add("prevEdge", tr("Previous transition"), tr("Previous transition (Left)"),
                makeIcon(Icon::PrevEdge)),
            &QAction::triggered, this, [this] { wave_->jumpToEdge(-1); });
    connect(add("nextEdge", tr("Next transition"), tr("Next transition (Right)"), makeIcon(Icon::NextEdge)),
            &QAction::triggered, this, [this] { wave_->jumpToEdge(+1); });
    connect(add("goEnd", tr("End"), tr("Go to the end of the simulation (End)"), makeIcon(Icon::GoEnd)),
            &QAction::triggered, this, [this] { wave_->goToEnd(); });

    addSeparator();
    addWidget(new QLabel(tr(" Go to: "), this));
    edit_ = new QLineEdit(this);
    edit_->setObjectName("goToEdit");
    edit_->setPlaceholderText(tr("e.g. 1500 ps or 12.5 ns"));
    edit_->setFixedWidth(150);
    edit_->setToolTip(tr("Enter a time with unit (fs, ps, ns, us, ms, s). Without unit, ns is assumed."));
    addWidget(edit_);
    connect(edit_, &QLineEdit::returnPressed, this, [this] { goToText(edit_->text()); });
    connect(add("goTo", tr("Go"), tr("Go to the entered time"), makeIcon(Icon::GoTo)), &QAction::triggered,
            this, [this] { goToText(edit_->text()); });

    addSeparator();
    connect(add("zoomIn", tr("Zoom in"), tr("Zoom in (+)"), makeIcon(Icon::ZoomIn)), &QAction::triggered,
            this, [this] { wave_->zoomBy(1.5); });
    connect(add("zoomOut", tr("Zoom out"), tr("Zoom out (-)"), makeIcon(Icon::ZoomOut)), &QAction::triggered,
            this, [this] { wave_->zoomBy(1.0 / 1.5); });
    connect(add("zoomFit", tr("Fit"), tr("Show the whole simulation (F)"), makeIcon(Icon::ZoomFit)),
            &QAction::triggered, this, [this] { wave_->zoomFull(); });

    addSeparator();
    connect(add("addCursor", tr("Add cursor"),
                tr("Add a cursor (shift-click also adds one). The time between cursors is shown."),
                makeIcon(Icon::AddCursor)),
            &QAction::triggered, this, [this] { wave_->addCursor(); });
    connect(add("removeCursor", tr("Remove cursor"), tr("Remove the active cursor"),
                makeIcon(Icon::RemoveCursor)),
            &QAction::triggered, this, [this] { wave_->removeCursor(wave_->activeCursor()); });
    connect(add("addDivider", tr("Add divider"), tr("Insert a divider line after the selected row"),
                makeIcon(Icon::Divider)),
            &QAction::triggered, this, [this] { wave_->promptInsertDivider(); });
}

bool WaveToolBar::goToText(const QString& text) {
    const int exp = wave_->store() != nullptr ? wave_->store()->timescaleExponent() : -15;
    const auto t = parseTime(text.toStdString(), exp);
    if (!t) {
        edit_->setStyleSheet("QLineEdit { border: 1px solid #ff4040; }");
        return false;
    }
    edit_->setStyleSheet({});
    wave_->goToTime(*t);
    return true;
}

} // namespace lvd::gui
