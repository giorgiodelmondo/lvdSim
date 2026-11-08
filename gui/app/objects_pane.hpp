// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvdwave/store.hpp>

#include <QStringList>
#include <QWidget>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace lvd::gui {

/// Browser of the design hierarchy and its signals, built from the waveform store. Double-click, the
/// context menu or Return adds the selected signals (or everything in the selected scopes) to the wave
/// window. The filter box hides signals whose name does not match (* and ? wildcards, no case).
class ObjectsPane : public QWidget {
    Q_OBJECT
public:
    explicit ObjectsPane(QWidget* parent = nullptr);

    /// Rebuilds the tree. Signal definitions must be complete; pass nullptr to clear.
    void setStore(const lvd::wave::WaveStore* store);

    /// Show only the signals of one scope (dotted path, with its sub-scopes below). An empty or unknown path
    /// shows the whole hierarchy.
    void setScope(const QString& dottedPath);
    QString scope() const { return scope_; }

    int scopeCount() const noexcept { return scopes_; }
    int signalCount() const noexcept { return signals_; }
    /// Paths (dotted, as in the store) of the signals that would be added for the current selection.
    QStringList selectedSignalPaths() const;
    void setFilter(const QString& text);
    int visibleSignalCount() const;

    QTreeWidget* tree() const noexcept { return tree_; }

signals:
    /// Paths of signals to show in the wave window, in display order.
    void addToWave(const QStringList& paths);

protected:
    void contextMenuEvent(QContextMenuEvent*) override;

private:
    void rebuild();
    void populate(QTreeWidgetItem* parent, lvd::wave::ScopeId scope, bool ownSignals);
    void collect(QTreeWidgetItem* item, QStringList& out) const;
    bool applyFilter(QTreeWidgetItem* item, const QString& pattern);
    void activate(const QStringList& paths);

    const lvd::wave::WaveStore* store_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QString scope_;
    int scopes_ = 0;
    int signals_ = 0;
};

} // namespace lvd::gui
