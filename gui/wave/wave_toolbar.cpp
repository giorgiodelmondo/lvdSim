// SPDX-License-Identifier: GPL-3.0-or-later
#include "wave/wave_toolbar.hpp"

#include "wave/icons.hpp"
#include "wave/view_state.hpp"
#include "wave/wave_widget.hpp"

#include <QAction>
#include <QActionGroup>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QToolButton>

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
    connect(add("prevEdge", tr("Previous edge"),
                tr("Previous edge of the selected signals (Left or Shift+Tab); the edge button chooses any, "
                   "rising "
                   "or falling"),
                makeIcon(Icon::PrevEdge)),
            &QAction::triggered, this, [this] { wave_->jumpToEdge(-1); });
    connect(add("nextEdge", tr("Next edge"),
                tr("Next edge of the selected signals (Right or Tab); the edge button chooses any, rising or "
                   "falling"),
                makeIcon(Icon::NextEdge)),
            &QAction::triggered, this, [this] { wave_->jumpToEdge(+1); });

    // Which edges the buttons and keys look for.
    edgeButton_ = new QToolButton(this);
    edgeButton_->setObjectName("edgeKind");
    edgeButton_->setPopupMode(QToolButton::InstantPopup);
    edgeButton_->setToolTip(tr("Edges to step through: any change, rising or falling (single-bit rows)"));
    auto* edgeMenu = new QMenu(edgeButton_);
    auto* edgeGroup = new QActionGroup(edgeButton_);
    struct EdgeEntry {
        const char* name;
        QString text;
        Icon icon;
        lvd::wave::EdgeKind kind;
    };
    const EdgeEntry entries[] = {
        {"edgeAny", tr("Any edge"), Icon::AnyEdge, lvd::wave::EdgeKind::Any},
        {"edgeRising", tr("Rising edge"), Icon::RisingEdge, lvd::wave::EdgeKind::Rising},
        {"edgeFalling", tr("Falling edge"), Icon::FallingEdge, lvd::wave::EdgeKind::Falling},
    };
    for (const EdgeEntry& e : entries) {
        QAction* a = edgeMenu->addAction(makeIcon(e.icon), e.text);
        a->setObjectName(e.name);
        a->setCheckable(true);
        a->setChecked(e.kind == wave_->edgeKind());
        edgeGroup->addAction(a);
        const lvd::wave::EdgeKind kind = e.kind;
        connect(a, &QAction::triggered, this, [this, kind] { wave_->setEdgeKind(kind); });
    }
    edgeButton_->setMenu(edgeMenu);
    auto syncEdge = [this, edgeMenu](lvd::wave::EdgeKind kind) {
        static constexpr const char* kNames[] = {"edgeAny", "edgeRising", "edgeFalling"};
        if (auto* a = edgeMenu->findChild<QAction*>(kNames[static_cast<int>(kind)])) {
            a->setChecked(true);
            edgeButton_->setIcon(a->icon());
            edgeButton_->setText(a->text());
        }
    };
    syncEdge(wave_->edgeKind());
    connect(wave_, &WaveWidget::edgeKindChanged, this, syncEdge);
    addWidget(edgeButton_);
    connect(add("goEnd", tr("End"), tr("Go to the end of the simulation (End)"), makeIcon(Icon::GoEnd)),
            &QAction::triggered, this, [this] { wave_->goToEnd(); });

    find_ =
        add("findValue", tr("Find value..."),
            tr("Find a value or a threshold crossing on the selected signals (Ctrl+F; F3 / Shift+F3 repeat)"),
            makeIcon(Icon::FindValue));
    find_->setShortcut(QKeySequence::Find);
    find_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    wave_->addAction(find_); // Ctrl+F while the wave window has the focus
    connect(find_, &QAction::triggered, this, [this] { wave_->showFindDialog(); });

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

    follow_ = add("follow", tr("Follow"), tr("Follow simulation (stay at end)"), makeIcon(Icon::Follow));
    follow_->setCheckable(true);
    follow_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
    follow_->setShortcutContext(Qt::WindowShortcut);
    follow_->setStatusTip(
        tr("Keep the right edge of the view at the end of the simulation as it runs (Ctrl+Shift+F)"));
    follow_->setChecked(wave_->follow());
    connect(follow_, &QAction::toggled, this, [this](bool on) { wave_->setFollow(on); });
    connect(wave_, &WaveWidget::followChanged, this, [this](bool on) {
        if (follow_->isChecked() != on)
            follow_->setChecked(on);
    });

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
