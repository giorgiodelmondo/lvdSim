// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QToolBar>

class QLineEdit;
class QToolButton;

namespace lvd::gui {

class WaveWidget;

/// Toolbar for a WaveWidget: go to the beginning or end of the simulation, previous and next
/// edge (of the kind chosen with the edge button: any, rising, falling), find a value, go to an entered time
/// ("1500 ps", "12.5 ns"), zoom, cursors and dividers.
///
/// Actions have object names (goStart, prevEdge, nextEdge, goEnd, edgeAny, edgeRising, edgeFalling,
/// findValue, goTo, zoomIn, zoomOut, zoomFit, addCursor, removeCursor, addDivider, follow) so tests and
/// shortcuts can find them; the edge kind button is the tool button named edgeKind. findValue has the
/// shortcut Ctrl+F while the wave window or the toolbar has the focus.
class WaveToolBar : public QToolBar {
    Q_OBJECT
public:
    explicit WaveToolBar(WaveWidget* wave, QWidget* parent = nullptr);

    /// Parse the text as a time and move there. A number without unit is in ns. Returns false (and marks
    /// the entry red) if the text is not a time.
    bool goToText(const QString& text);
    QLineEdit* timeEdit() const noexcept { return edit_; }
    /// The checkable "Follow simulation" action (object name follow). It mirrors WaveWidget::follow() in both
    /// directions; add it to a menu to get the same toggle there.
    QAction* followAction() const noexcept { return follow_; }
    QAction* findAction() const noexcept { return find_; }
    QToolButton* edgeKindButton() const noexcept { return edgeButton_; }

private:
    QAction* add(const QString& objectName, const QString& text, const QString& tip, const QIcon& icon = {});

    WaveWidget* wave_;
    QLineEdit* edit_ = nullptr;
    QAction* follow_ = nullptr;
    QAction* find_ = nullptr;
    QToolButton* edgeButton_ = nullptr;
};

} // namespace lvd::gui
