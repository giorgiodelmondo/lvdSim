// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rhi/gpu_wave_renderer.hpp"
#include "wave/irenderer.hpp"
#include "wave/row_source.hpp"
#include "wave/tile_cache.hpp"
#include "wave/view_state.hpp"

#include <lvdwave/display_edit.hpp>
#include <lvdwave/display_model.hpp>
#include <lvdwave/lod.hpp>
#include <lvdwave/store.hpp>
#include <lvdwave/wave_commands.hpp>

#include <QColor>
#include <QWidget>

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QLineEdit;
class QMenu;
class QScrollBar;
class QTimer;

namespace lvd::gui {

class RhiWaveCanvas;
class WaveOverlay;

/// Severity of an event marker (an HDL report or assertion), in increasing order of importance.
enum class EventSeverity { Note, Warning, Error, Failure };

/// A point event shown as a small arrow in the marker band above the rows.
struct EventMarker {
    long long timeFs = 0;  ///< simulation time in femtoseconds
    EventSeverity severity = EventSeverity::Note;
    QString text;          ///< shown in the tooltip
};

/// Markers that fall in one pixel column of the marker band, merged into one arrow.
struct EventGroup {
    int x = 0;            ///< widget x of the arrow centre
    int count = 0;        ///< number of events merged
    EventSeverity severity = EventSeverity::Note;  ///< the highest severity among them
    std::size_t first = 0;  ///< index of the earliest event in the (time sorted) marker list
};

/// The wave window: time ruler, name column, value column and the waveform area.
///
/// Rows: signals, virtual buses, groups and dividers. A vector signal has a triangle that expands it into
/// one row per bit. Any set of rows or bits, even sparse and from different signals, can be selected
/// (ctrl-click, shift-click for a range) and turned into a virtual bus.
///
/// Cursors: click or drag moves the active cursor, shift-click adds a cursor, dragging near a cursor
/// line grabs that cursor. The distance between neighbouring cursors is drawn as an arrow with the
/// time difference.
///
/// Keys: Left/Right jump the active cursor to the previous/next transition of the selected rows (all
/// rows if none is selected), ctrl+wheel zooms around the pointer, middle drag pans, F fits the trace,
/// +/- zoom, Delete removes the selection, Ctrl+B makes a virtual bus.
///
/// Arranging rows (see docs/display-format.md): Ctrl+Up / Ctrl+Down move the selected rows among their
/// siblings, Ctrl+Shift+Up / Down to the top or bottom of their group, Ctrl+G wraps the selection in a new
/// group, Ctrl+Shift+G ungroups, F2 renames a group, divider, bus or signal label in place, Ctrl+Z undoes
/// and Ctrl+Y (or Ctrl+Shift+Z) redoes. Rows, groups and dividers can also be dragged by the name column
/// (a line shows where they land, a highlighted group row means "into this group", Escape cancels) and
/// the context menu has all of it. Moving a row keeps its identity: its tile cache entries, selection and
/// cursors stay as they are.
class WaveWidget : public QWidget {
    Q_OBJECT
public:
    static constexpr int kMaxCursors = 8;

    explicit WaveWidget(QWidget* parent = nullptr);
    ~WaveWidget() override;

    void setStore(std::shared_ptr<const lvd::wave::WaveStore> store);
    const lvd::wave::WaveStore* store() const noexcept { return store_.get(); }

    void setDisplayModel(lvd::wave::DisplayModel model);
    const lvd::wave::DisplayModel& displayModel() const noexcept { return model_; }

    void setRenderer(std::unique_ptr<IRenderer> renderer);
    const IRenderer& renderer() const noexcept { return *renderer_; }
    const TileCache& tileCache() const noexcept { return cache_; }

    const ViewState& view() const noexcept { return view_; }
    void setView(double startTime, double timePerPixel);
    void zoomFull();
    void zoomBy(double factor);  ///< >1 zooms in around the centre of the wave area

    // ---- Live data (a simulation that is still running) -----------------------------------------
    /// Call after a live source appended to the store: refreshes the rows and the view. Until the user
    /// zooms or pans, the view keeps fitting the whole run; afterwards it only follows the end if it was
    /// showing the end.
    void dataChanged();
    bool autoFit() const noexcept { return autoFit_; }
    /// What `add wave` does: adds the matching signals. Returns how many rows were added.
    std::size_t addWaveSignals(const lvd::wave::AddWaveRequest& request);
    void clearWave();  ///< removes all rows (`delete wave *`)
    /// The rows as a script of `add wave` commands (see lvd::wave::exportDoScript).
    std::string doScript() const;
    /// Applies the `add wave` commands of a script. Needs a store, otherwise nothing is added.
    lvd::wave::ImportResult loadDoScript(const std::string& script);

    // ---- Rendering backend ---------------------------------------------------------------------
    enum class Backend { Cpu, Gpu };
    /// Cpu draws the wave area with QPainter. Gpu draws it with the GPU through Qt's rendering hardware
    /// interface (cached tiles stay on the GPU). Gpu is a request: if the window cannot render with the GPU,
    /// the widget falls back to Cpu on its own and renderBackendChanged() is emitted. The default comes from
    /// nothing: a bare widget starts on Cpu; the applications choose with resolveRenderPolicy() (render_policy.hpp).
    void setRenderBackend(Backend backend);
    Backend renderBackend() const noexcept { return gpuActive_ ? Backend::Gpu : Backend::Cpu; }
    /// The backend the default policy asks for now (LVDSIM_RENDERER, otherwise Auto = GPU on real platforms).
    static Backend backendFromEnvironment();
    /// Why a requested GPU is not in use ("platform offscreen", "no QRhi", "shader load failed"); empty when
    /// the GPU is active or was not requested.
    QString fallbackReason() const { return fallbackReason_; }
    Backend requestedBackend() const noexcept { return requested_; }
    /// What the GPU canvas draws, as plain data (also used by tests and benchmarks).
    GpuFrame buildGpuFrame(const QSize& size);
    /// The widget as an image. Unlike grab() this includes the GPU canvas, which grab() cannot see.
    QImage snapshot();
    const RhiWaveCanvas* gpuCanvas() const noexcept { return canvas_; }

    // ---- Cursors -------------------------------------------------------------------------------
    int cursorCount() const noexcept { return static_cast<int>(cursors_.size()); }
    std::optional<double> cursorTime(int which) const noexcept;
    /// Moves cursor `which`. If which equals cursorCount() a new cursor is appended. An empty time removes
    /// the cursor.
    void setCursorTime(int which, std::optional<double> t);
    int activeCursor() const noexcept { return activeCursor_; }
    void setActiveCursor(int which);
    /// Adds a cursor (at the middle of the view by default), makes it active and returns its index, or -1
    /// when the maximum is reached.
    int addCursor(std::optional<double> t = std::nullopt);
    void removeCursor(int which);

    // ---- Event markers (HDL reports) -----------------------------------------------------------
    /// Replaces all markers; they are sorted by time. Markers live in the band between the ruler and the first
    /// row; they never cover the rows. A click on an arrow moves cursor 1 there.
    void setEventMarkers(std::vector<EventMarker> markers);
    /// Adds one marker (O(1) when it is not earlier than the last one).
    void addEventMarker(EventMarker marker);
    void clearEventMarkers();
    std::size_t eventMarkerCount() const noexcept { return events_.size(); }
    const EventMarker& eventMarker(std::size_t index) const { return events_.at(index); }
    /// Arrows currently visible, one per pixel column, left to right. Cost: O(columns * log events) at most.
    std::vector<EventGroup> visibleEventGroups() const;
    /// The arrow under a point of the marker band (a few pixels of tolerance), if any.
    std::optional<EventGroup> eventGroupAt(const QPoint& pos) const;
    /// Tooltip text of a group: severity, time and message (first messages and the count when merged).
    QString eventGroupText(const EventGroup& group) const;
    /// Sets cursor 1 (created if there is none) to a time in femtoseconds and makes it active. The view keeps its zoom
    /// and scrolls only if the time is not visible.
    void showTimeFs(long long fs);

    // ---- Navigation ----------------------------------------------------------------------------
    /// Move the active cursor to the previous (dir < 0) or next (dir > 0) transition of the selected rows,
    /// or of all rows if nothing is selected. Returns false if there is none.
    bool jumpToEdge(int dir);
    void goToStart();            ///< cursor and view to the beginning of the simulation
    void goToEnd();              ///< cursor to the end, view scrolled so that it is visible
    void goToTime(double time);  ///< cursor to a time (timescale units), view centred on it

    // ---- Rows and selection --------------------------------------------------------------------
    int rowCount() const noexcept { return static_cast<int>(rows_.size()); }
    std::string rowName(int row) const;
    std::string rowValueText(int row) const;  ///< value at the active cursor, empty if none, "-" before the first change and after the end of the data
    WaveClass rowClass(int row) const;        ///< valid only if rowHasSource(row)
    bool rowHasSource(int row) const;
    int rowBit(int row) const;                ///< bit index for per-bit rows, -1 otherwise
    bool rowExpandable(int row) const;        ///< multi-bit signal that can show its bits
    std::optional<QColor> rowColor(int row) const;
    void selectRow(int row, bool additive = false);
    void selectRange(int fromRow, int toRow);
    void clearSelection();
    int selectedCount() const noexcept { return static_cast<int>(selected_.size()); }

    // ---- Arranging rows: move, group, rename, undo ----------------------------------------------
    // Every call below is one undo step when it changes something, keeps the selection (grouping selects the new
    // group, ungrouping selects the former children), scrolls so that the moved rows are visible and emits
    // structureChanged(). A refused edit changes nothing, sets lastEditError() and emits editRejected().
    using ItemId = lvd::wave::ItemId;
    static constexpr std::size_t kAppend = static_cast<std::size_t>(-1);  ///< index meaning "at the end"

    /// Ids of the selected whole items (per-bit rows are not items), in selection order.
    std::vector<ItemId> selectedItemIds() const;
    ItemId itemIdAtRow(int row) const;  ///< 0 if out of range
    int rowOfItem(ItemId id) const;     ///< first row of the item, -1 if it is not shown (collapsed parent)
    bool isRowSelected(int row) const;
    /// Moves the selected items by |delta| places among their siblings (negative: up). Each stays in its group.
    bool moveSelectedRows(int delta);
    bool moveSelectedRowsToEdge(bool toTop);  ///< top or bottom of the own group
    /// Moves items into a group (lvd::wave::kTopLevel for the top level) at an index of its child list
    /// (counted before the move, kAppend for the end). Items may come from different groups.
    bool moveRowsToGroup(const std::vector<ItemId>& ids, ItemId parent, std::size_t index = kAppend);
    bool moveSelectedToGroup(ItemId parent);  ///< moveRowsToGroup(selection, parent, kAppend)
    /// Wraps the selected items in a new group (default name "Group", made unique) where the first one was.
    bool groupSelected(const std::string& label = {});
    bool ungroupSelected();  ///< replaces the selected groups by their children
    /// Renames a group, divider, bus or signal label. Groups and buses need a name; see lvd::wave::renameItem.
    bool renameItem(ItemId id, const std::string& label);
    /// Inserts a divider above the first / below the last selected item (top / end if nothing is selected)
    /// and selects it.
    bool insertDividerAbove(const std::string& label = {});
    bool insertDividerBelow(const std::string& label = {});
    QString lastEditError() const { return lastEditError_; }

    // Undo and redo of the edits above, and of removing rows, making buses and changing radix, colour or
    // analog display. Expanded/collapsed state is not part of the history. Bounded to 100 steps; adding signals
    // from the simulator or loading a layout starts a new history.
    bool canUndo() const noexcept { return !undo_.empty(); }
    bool canRedo() const noexcept { return !redo_.empty(); }
    QString undoText() const;  ///< name of the edit undo() would revert, empty if none
    QString redoText() const;
    bool undo();
    bool redo();
    void clearUndoHistory();

    // Inline rename editor (F2, double click on a divider, "Rename" in the menu).
    /// Opens the editor over the name of a row (the single selected item by default). False if the row cannot
    /// be renamed (bit rows, nothing or several rows selected).
    bool beginRename(int row = -1);
    bool renaming() const noexcept { return editor_ != nullptr; }
    QLineEdit* renameEditor() const noexcept { return editor_; }
    /// Applies the editor's text. A refused name (empty group name) keeps the editor open and marks it.
    bool commitRename();
    void cancelRename();

    /// The context menu for a click on `row` (-1: not on a row), built from the current selection. The caller
    /// owns it. Actions have object names (moveUp, moveDown, moveTop, moveBottom, moveToGroupMenu,
    /// moveToTopLevel, moveToNewGroup, moveToGroup_<id>, groupSelected, ungroup, rename, insertDividerAbove,
    /// insertDividerBelow, delete, undo, redo, ...).
    QMenu* createContextMenu(int row);
    /// Adds the arranging actions (move, group, rename, divider, delete, undo and redo) for the current selection.
    void populateArrangeMenu(QMenu* menu);
    /// Replaces the dialog that asks for a group name; for tests. Returns the text or nothing if cancelled.
    using TextPrompt = std::function<std::optional<QString>(const QString& title, const QString& label, const QString& initial)>;
    void setTextPrompt(TextPrompt prompt) { prompt_ = std::move(prompt); }

    // Drag and drop of rows by the name column. The mouse handlers use these; tests may call them directly.
    struct RowDropTarget {
        bool valid = false;     ///< the drop would be accepted
        bool outside = false;   ///< not over the name column: a drop is ignored, nothing is reported
        ItemId parent = 0;      ///< destination group (0: top level)
        std::size_t index = 0;  ///< position in the destination's children
        bool intoGroup = false; ///< over the middle of a group row: highlight it instead of drawing a line
        int row = -1;           ///< row under the pointer
        int lineY = -1;         ///< widget y of the drop line
        int lineX = 0;          ///< widget x where the drop line starts (follows the nesting depth)
        QString reason;         ///< why it is refused
    };
    bool startRowDrag();                                  ///< drags the selected items; false if none
    RowDropTarget dropTargetAt(const QPoint& pos) const;  ///< what a drop at pos would do
    void updateRowDrag(const QPoint& pos);
    bool finishRowDrag(const QPoint& pos);                ///< drops; true if the rows were moved
    void cancelRowDrag();
    bool rowDragActive() const noexcept { return drag_ == Drag::Rows; }
    const RowDropTarget& rowDropIndicator() const noexcept { return dropTarget_; }
    /// One step of the scrolling that runs while a drag is near the top or bottom edge. Returns the scroll
    /// distance in pixels (0 when the pointer is not near an edge).
    int autoScrollStep();

    // ---- Editing the selection (each rebuilds the rows) -----------------------------------------
    void setRadixForSelection(lvd::wave::Radix radix);
    void toggleAnalogForSelection();
    void setColorForSelection(std::optional<QColor> color);
    bool groupSelection(const std::string& label = {});  ///< same as groupSelected()
    /// Build a virtual bus from the selected rows in display order (first is the most significant bit
    /// or member). Per-bit rows contribute single bits, signal rows the whole signal. Needs two or more
    /// bits in total. The bus is inserted after the last selected row.
    bool busFromSelection(const std::string& label = {});
    void removeSelection();
    void toggleGroup(int row);
    void toggleBits(int row);
    /// Insert a divider after the last selected row (at the end if nothing is selected).
    void insertDivider(const std::string& label);  ///< same as insertDividerBelow()
    void promptInsertDivider();  ///< asks for the label, then insertDivider

    // ---- Geometry, for hit testing and tests ---------------------------------------------------
    int rulerHeight() const noexcept { return kRulerHeight; }
    /// The band for event arrows and cursor delta labels: y from rulerHeight() to rowsTop().
    int eventBandHeight() const noexcept { return kBandHeight; }
    int eventBandTop() const noexcept { return kRulerHeight; }
    /// Y of the first row (with no vertical scroll): below the ruler and the event band.
    int rowsTop() const noexcept { return kRowsTop; }
    int rowHeight() const noexcept { return kRowHeight; }
    int waveLeft() const noexcept { return nameWidth_ + valueWidth_; }
    int waveWidth() const;
    /// Y of the top of row in widget coordinates.
    int rowTop(int row) const;

signals:
    void renderBackendChanged();
    void cursorsChanged();
    void viewChanged();
    void selectionChanged();
    /// The tree changed: rows were moved, grouped, renamed, removed or added. For marking a layout as modified.
    void structureChanged();
    /// An edit was refused (message for the status bar or transcript).
    void editRejected(const QString& message);
    void undoStateChanged();

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    bool event(QEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;

private:
    friend class WaveOverlay;
    static constexpr int kRulerHeight = 40;
    static constexpr int kBandHeight = 18;
    static constexpr int kRowsTop = kRulerHeight + kBandHeight;
    static constexpr int kRowHeight = 22;

    struct Row {
        lvd::wave::DisplayItem* item = nullptr;
        int bit = -1;  ///< >= 0 for the per-bit rows of an expanded vector (item is the vector)
        int depth = 0;
        int y = 0;  ///< top in content coordinates (below the ruler)
        int h = kRowHeight;
        bool expandable = false;
        std::shared_ptr<RowSource> source;
        RowStyle style;
        std::uint64_t revision = 0;
        const void* cacheKey = nullptr;
    };

    /// A selected row: an item by id (survives every edit) and the bit of a per-bit row, -1 for the item itself.
    struct RowKey {
        ItemId id;
        int bit;
        bool operator==(const RowKey& o) const noexcept { return id == o.id && bit == o.bit; }
    };

    struct Snapshot {
        lvd::wave::DisplayModel model;
        std::vector<RowKey> selection;
        QString text;
    };
    using EditFn = std::function<lvd::Result<lvd::wave::EditOutcome>(lvd::wave::DisplayModel&)>;
    bool runEdit(const QString& text, const EditFn& fn, const std::function<void(const lvd::wave::EditOutcome&)>& select = {},
                 ItemId reveal = 0);
    void pushUndo(Snapshot before);
    void restore(Snapshot snap);
    void afterEdit();  ///< rebuild, prune the selection, tell the world
    void ensureRowVisible(int row);
    void positionEditor();
    void closeEditor();
    std::optional<QString> promptText(const QString& title, const QString& label, const QString& initial);
    void drawDropIndicator(QPainter& p) const;
    void addGroupTargets(QMenu* menu, const std::vector<ItemId>& ids);

    void rebuildRows();
    const lvd::wave::NumericIndex* numericIndexFor(lvd::wave::SignalId id, bool isSigned);
    int rowAtY(int y) const;  ///< widget y to row index, -1 if none
    bool isSelected(const Row& row) const;
    std::vector<lvd::wave::DisplayItem*> selectedItems() const;  ///< whole items only, no per-bit rows
    void updateScrollBar();
    void clampView();
    void drawRuler(QPainter& p) const;
    void drawRow(QPainter& p, int rowIndex, int top);
    void drawBusLabels(QPainter& p, const Row& row, int top) const;
    /// End of the recorded data (simulation time reached): nothing is drawn after it.
    Time dataEnd() const;
    /// Widget x of the first pixel column after the end of the data.
    int noDataLeft() const;
    QColor noDataTint() const;
    void drawCursors(QPainter& p) const;
    void drawEventMarkers(QPainter& p) const;
    double fsPerUnit() const;  ///< femtoseconds in one timescale unit of the store (1 without a store)
    std::vector<EventGroup> eventGroupsBetween(double xFrom, double xTo) const;  ///< x relative to the wave area
    void paintOverlay(QPainter& p) const;  ///< bus value labels and cursors, painted above the GPU canvas
    void updateGpuGeometry();
    void fallBackToCpu();
    std::string nameOf(const Row& row) const;
    void emitViewChanged();
    int effectiveWaveRight() const;
    int cursorNear(int x) const;
    void ensureVisible(double t);
    std::optional<double> activeCursorTime() const;

    std::shared_ptr<const lvd::wave::WaveStore> store_;
    lvd::wave::DisplayModel model_;
    Palette palette_;  // must be declared before renderer_, which is constructed from it
    std::unique_ptr<IRenderer> renderer_;
    TileCache cache_;
    ViewState view_;

    std::vector<Row> rows_;
    std::vector<RowKey> selected_;
    int anchorRow_ = -1;
    std::deque<Snapshot> undo_, redo_;
    QString lastEditError_;
    QLineEdit* editor_ = nullptr;
    ItemId renameId_ = 0;
    bool editorBusy_ = false;  ///< the editor is being hidden or moved by us: ignore its focus loss
    TextPrompt prompt_;
    QTimer* autoScroll_ = nullptr;
    int pressRow_ = -1;
    QPoint pressPos_;
    bool rowPressPending_ = false;  ///< a press on a row that may turn into a drag
    bool deferSelect_ = false;      ///< the press was on a selected row: collapse the selection on release if no drag
    std::vector<ItemId> dragIds_;
    RowDropTarget dropTarget_;
    std::map<std::pair<lvd::wave::SignalId, bool>, std::unique_ptr<lvd::wave::NumericIndex>> indexes_;
    std::map<std::string, std::shared_ptr<RowSource>> busCache_;
    std::map<std::pair<lvd::wave::SignalId, int>, std::shared_ptr<RowSource>> bitCache_;
    std::uint64_t storeRevision_ = 1;
    RhiWaveCanvas* canvas_ = nullptr;
    QWidget* overlay_ = nullptr;
    Backend requested_ = Backend::Cpu;
    bool gpuActive_ = false;
    QString fallbackReason_;
    bool autoFit_ = true;
    double lastEnd_ = 0.0;

    std::vector<EventMarker> events_;  ///< sorted by time
    /// cumulative counts per severity: eventPrefix_[s][i] events among the first i with severity s
    std::vector<std::uint32_t> eventPrefix_[4];

    std::vector<double> cursors_;
    int activeCursor_ = 0;
    int nameWidth_ = 220;
    int valueWidth_ = 110;
    int scrollY_ = 0;
    QScrollBar* vbar_ = nullptr;

    enum class Drag { None, Cursor, Pan, NameSplitter, ValueSplitter, Rows } drag_ = Drag::None;
    int dragCursor_ = 0;
    QPoint lastMouse_;
};

}  // namespace lvd::gui
