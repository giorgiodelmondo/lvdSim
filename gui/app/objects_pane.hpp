// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sim/parsers/json_replies.hpp"

#include <lvdwave/store.hpp>

#include <QHash>
#include <QPixmap>
#include <QStringList>
#include <QWidget>

#include <functional>

class QDrag;
class QLineEdit;
class QMenu;
class QMimeData;
class QTreeWidget;
class QTreeWidgetItem;

namespace lvd::gui {

/// Browser of the design hierarchy and its signals, built from the waveform store. Double-click on a signal
/// shows its declaration in the Source pane (viewDeclarationRequested(); the simulator says where it is,
/// see SimWindow). Return, the context menu (Add to Wave) or a drag adds the selected signals (or everything
/// in the selected scopes) to the wave window. The context menu also has View Declaration, Add to List,
/// Add to Memory (memories only) and Copy Path. The filter box hides signals whose name does not match (*
/// and ? wildcards, no case).
///
/// Signals (and scopes, which stand for their signals) can be dragged onto the wave window: the drag carries
/// the full `add wave` paths in a custom mime type and as plain text, and shows the names and their count.
/// Each signal has an icon for its kind (input, output, inout, buffer, internal; a neutral ring when the
/// simulator did not report it). The modes come from the store (the kernel writes them into the live stream,
/// FST files carry them too); whoever learns more (see docs/protocol.md, `describe -json` member "mode")
/// hands them over with setPortModes(), which wins over the store.
///
/// The elements of a memory (an array of words, dumped as `mem[0][7:0]`, `mem[1][7:0]`, ...; see
/// lvd::wave::findMemories) are grouped under one collapsed item of type "memory"; its context menu opens it
/// in the Memory window (openMemory()). Adding the item to the wave adds every element.
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

    /// Port modes by dotted store path ("t.dut.clk"). Merged into what is known, the icons of the rows shown
    /// change at once; other signals show the direction recorded in the store (PortMode::Unknown if none).
    /// Cleared by setStore().
    void setPortModes(const QHash<QString, PortMode>& modes);
    PortMode portModeOf(const QString& dottedPath) const;

    /// What a drag of the current selection carries: the `add wave` paths ("/t/dut/clk") in the custom mime
    /// type (lvd::gui::kSignalPathsMime) and as text/plain. The caller owns it; null if no signal is
    /// selected.
    QMimeData* mimeDataForSelection() const;
    /// The picture that follows the pointer: a few names and the count.
    static QPixmap dragPixmap(const QStringList& names, qreal devicePixelRatio = 1.0);
    /// Runs the drag; replaced by tests, which cannot enter the platform's modal drag loop.
    using DragRunner = std::function<Qt::DropAction(QDrag*)>;
    void setDragRunner(DragRunner runner) { dragRunner_ = std::move(runner); }
    /// Starts dragging the selected signals. Called by the tree when the pointer has moved far enough; false
    /// if nothing is selected that could be dropped.
    bool startSignalDrag();

    /// Tells whether View Declaration can be used now: the function returns why not (shown as tool tip of the
    /// disabled entry), or an empty text when it can. Without a gate the entry is enabled.
    void setDeclarationGate(std::function<QString()> gate) { declarationGate_ = std::move(gate); }
    /// The dotted store path whose declaration an item stands for: a signal or a memory; empty for scopes.
    QString declarationPathOf(const QTreeWidgetItem* item) const;
    /// Return or Enter in the tree: adds the selection to the wave window.
    void activateSelection() { activate(selectedSignalPaths()); }
    /// Double-click on an item: View Declaration for a signal or a memory (scopes only expand).
    void itemDoubleClicked(QTreeWidgetItem* item);
    /// The context menu for the current item and selection; caller owns it (parented to the pane).
    QMenu* createContextMenu();

signals:
    /// Paths of signals to show in the wave window, in display order.
    void addToWave(const QStringList& paths);
    /// Show this memory (dotted store path, "top.mem") in the Memory window.
    void openMemory(const QString& path);
    /// Add these signals (dotted store paths) to the List window.
    void addToList(const QStringList& paths);
    /// Show where the signal or memory (dotted store path) is declared.
    void viewDeclarationRequested(const QString& path);

protected:
    void contextMenuEvent(QContextMenuEvent*) override;

private:
    void rebuild();
    void populate(QTreeWidgetItem* parent, lvd::wave::ScopeId scope, bool ownSignals);
    void collect(QTreeWidgetItem* item, QStringList& out) const;
    bool applyFilter(QTreeWidgetItem* item, const QString& pattern);
    void activate(const QStringList& paths);
    /// The memory a tree item stands for (the memory item or one of its elements), or empty.
    QString memoryPathOf(const QTreeWidgetItem* item) const;

public:
    /// Dotted path of the memory under the current item, empty if it is no memory (for the context menu).
    QString currentMemoryPath() const;
    /// The tree item of a memory, or nullptr.
    QTreeWidgetItem* memoryItem(const QString& dottedPath) const;

private:
    void applyMode(QTreeWidgetItem* item, PortMode mode) const;

    const lvd::wave::WaveStore* store_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QString scope_;
    int scopes_ = 0;
    int signals_ = 0;
    QHash<QString, PortMode> modes_;
    QHash<lvd::wave::SignalId, QString> memoryOf_; ///< element entry -> dotted path of its memory
    QHash<QString, QTreeWidgetItem*> memoryItems_;
    DragRunner dragRunner_;
    std::function<QString()> declarationGate_;
};

} // namespace lvd::gui
