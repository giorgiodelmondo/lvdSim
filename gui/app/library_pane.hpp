// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <libraries.hpp>

#include <QWidget>

class QMenu;
class QTreeWidget;
class QTreeWidgetItem;

namespace lvd::gui {

/// Browser of the design libraries and the units in them. Double-click only expands or collapses; a
/// simulation is started from the context menu (Simulate) or with the vsim command.
class LibraryPane : public QWidget {
    Q_OBJECT
public:
    explicit LibraryPane(QWidget* parent = nullptr);

    void setLibraries(const std::vector<lvd::tools::LibraryInfo>& libraries);
    int libraryCount() const noexcept { return libraries_; }
    int unitCount() const noexcept { return units_; }
    QTreeWidget* tree() const noexcept { return tree_; }
    /// The context menu for item (may be null: then Simulate is disabled). Caller owns it (parented to the pane).
    QMenu* createContextMenu(QTreeWidgetItem* item);

signals:
    /// "library.unit", ready to be given to vsim.
    void simulateRequested(const QString& designUnit);
    void refreshRequested();

protected:
    void contextMenuEvent(QContextMenuEvent*) override;

private:
    QTreeWidget* tree_ = nullptr;
    int libraries_ = 0;
    int units_ = 0;
};

}  // namespace lvd::gui
