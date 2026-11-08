// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvdwave/store.hpp>

#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;

namespace lvd::gui {

/// The design hierarchy as a tree of scopes (instances, blocks, records), without the signals. Selecting
/// a scope tells the objects pane which scope's signals to list.
class StructurePane : public QWidget {
    Q_OBJECT
public:
    explicit StructurePane(QWidget* parent = nullptr);

    /// Rebuilds the tree; nullptr clears it. The first row, "design", stands for the whole hierarchy.
    void setStore(const lvd::wave::WaveStore* store);
    int scopeCount() const noexcept { return scopes_; }
    QTreeWidget* tree() const noexcept { return tree_; }

signals:
    /// Dotted path of the selected scope; empty for "design".
    void scopeSelected(const QString& path);

private:
    void populate(QTreeWidgetItem* parent, lvd::wave::ScopeId scope);

    const lvd::wave::WaveStore* store_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    int scopes_ = 0;
};

}  // namespace lvd::gui
