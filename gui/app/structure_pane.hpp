// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvdwave/store.hpp>

#include <QWidget>

#include <functional>

class QMenu;
class QTreeWidget;
class QTreeWidgetItem;

namespace lvd::gui {

/// The design hierarchy as a tree of scopes (instances, blocks, records), without the signals. Selecting
/// a scope tells the objects pane which scope's signals to list. Double-click (or View Definition in the
/// context menu) shows the design unit of an instance (the architecture or module) or the statement of a
/// block or generate; View Instantiation shows the statement in the parent that creates it.
class StructurePane : public QWidget {
    Q_OBJECT
public:
    explicit StructurePane(QWidget* parent = nullptr);

    /// Rebuilds the tree; nullptr clears it. The first row, "design", stands for the whole hierarchy.
    void setStore(const lvd::wave::WaveStore* store);
    int scopeCount() const noexcept { return scopes_; }
    QTreeWidget* tree() const noexcept { return tree_; }

    /// Why View Definition and View Instantiation cannot be used now (empty: they can); see ObjectsPane.
    void setDefinitionGate(std::function<QString()> gate) { gate_ = std::move(gate); }
    /// Dotted path of an item's scope; empty for "design" and for packages (no instance to look up).
    QString definitionPathOf(const QTreeWidgetItem* item) const;
    /// The context menu for an item (may be null). Caller owns it (parented to the pane).
    QMenu* createContextMenu(QTreeWidgetItem* item);

signals:
    /// Dotted path of the selected scope; empty for "design".
    void scopeSelected(const QString& path);
    /// Show the design unit (or block/generate statement) of the scope (dotted store path).
    void viewDefinitionRequested(const QString& path);
    /// Show the statement that instantiates the scope in its parent.
    void viewInstantiationRequested(const QString& path);

protected:
    void contextMenuEvent(QContextMenuEvent*) override;

private:
    void populate(QTreeWidgetItem* parent, lvd::wave::ScopeId scope);

    const lvd::wave::WaveStore* store_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    int scopes_ = 0;
    std::function<QString()> gate_;
};

} // namespace lvd::gui
