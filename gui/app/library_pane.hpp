// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

#include <functional>
#include <libraries.hpp>

class QMenu;
class QTreeWidget;
class QTreeWidgetItem;

namespace lvd::gui {

/// Browser of the design libraries and the units in them. Double-click on a unit or an architecture shows its
/// source file (viewSourceRequested(); never a simulation); on a library it expands or collapses. A
/// simulation is started from the context menu (Simulate) or with the vsim command; the context menu also has
/// View Source.
class LibraryPane : public QWidget {
    Q_OBJECT
public:
    explicit LibraryPane(QWidget* parent = nullptr);

    void setLibraries(const std::vector<lvd::tools::LibraryInfo>& libraries);
    int libraryCount() const noexcept { return libraries_; }
    int unitCount() const noexcept { return units_; }
    QTreeWidget* tree() const noexcept { return tree_; }
    /// The context menu for item (may be null: then Simulate is disabled). Caller owns it (parented to the
    /// pane).
    QMenu* createContextMenu(QTreeWidgetItem* item);
    /// Why the source of a unit ("library.unit", architecture or empty) cannot be shown (tool tip of the
    /// disabled View Source); empty when it can. Without a gate View Source is enabled for every unit.
    void setSourceGate(std::function<QString(const QString& unit, const QString& architecture)> gate) {
        sourceGate_ = std::move(gate);
    }
    /// Double-click on an item: View Source for units and architectures, expand or collapse for libraries.
    void itemDoubleClicked(QTreeWidgetItem* item);

signals:
    /// "library.unit", ready to be given to vsim.
    void simulateRequested(const QString& designUnit);
    void refreshRequested();
    /// Show the source of "library.unit" (the architecture when not empty).
    void viewSourceRequested(const QString& designUnit, const QString& architecture);

protected:
    void contextMenuEvent(QContextMenuEvent*) override;

private:
    QTreeWidget* tree_ = nullptr;
    int libraries_ = 0;
    int units_ = 0;
    std::function<QString(const QString&, const QString&)> sourceGate_;
};

} // namespace lvd::gui
