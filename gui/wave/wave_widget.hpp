// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rhi/gpu_wave_renderer.hpp"
#include "wave/irenderer.hpp"
#include "wave/row_source.hpp"
#include "wave/tile_cache.hpp"
#include "wave/view_state.hpp"

#include <lvdwave/compare.hpp>
#include <lvdwave/display_edit.hpp>
#include <lvdwave/display_model.hpp>
#include <lvdwave/expression.hpp>
#include <lvdwave/lod.hpp>
#include <lvdwave/search.hpp>
#include <lvdwave/store.hpp>
#include <lvdwave/wave_commands.hpp>
#include <lvdwave/wave_space.hpp>

#include <QColor>
#include <QFont>
#include <QStringList>
#include <QWidget>

#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QLineEdit;
class QMenu;
class QScrollBar;
class QTimer;
class QDialog;

namespace lvd::gui {

class FindValueDialog;
class RhiWaveCanvas;
class WaveOverlay;

/// Severity of an event marker (an HDL report or assertion), in increasing order of importance.
enum class EventSeverity { Note, Warning, Error, Failure };

/// A point event shown as a small arrow in the marker band above the rows.
struct EventMarker {
    long long timeFs = 0; ///< simulation time in femtoseconds
    EventSeverity severity = EventSeverity::Note;
    QString text; ///< shown in the tooltip
};

/// Markers that fall in one pixel column of the marker band, merged into one arrow.
struct EventGroup {
    int x = 0;                                    ///< widget x of the arrow centre
    int count = 0;                                ///< number of events merged
    EventSeverity severity = EventSeverity::Note; ///< the highest severity among them
    std::size_t first = 0; ///< index of the earliest event in the (time sorted) marker list
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
/// Keys: Left/Right (and Tab/Shift+Tab) jump the active cursor to the previous/next edge of the selected rows
/// (all rows if none is selected), Ctrl+F finds a value, F3/Shift+F3 repeat the search, ctrl+wheel zooms
/// around the pointer, middle drag pans, F fits the trace, +/- zoom, Delete removes the selection, Ctrl+B
/// makes a virtual bus.
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
    void zoomFull();            ///< fits the whole run; a manual action, so it turns Follow off
    void zoomBy(double factor); ///< >1 zooms in around the centre of the wave area (the end while following)

    // ---- Scroll bars -------------------------------------------------------------------------------
    /// Horizontal bar (time): its range is the whole run known to the store, [start, end], in kHBarSteps
    /// steps; the thumb is the visible window and fills the bar when the view shows everything. It grows with
    /// live data. Moving it by the user pans the view and turns Follow off; every other view change moves the
    /// thumb without feeding back.
    QScrollBar* horizontalScrollBar() const noexcept { return hbar_; }
    /// Vertical bar (rows): pixel units, range = rows height - visible height. It drives the name, value and
    /// wave columns together because they are one widget painted with one scroll offset.
    QScrollBar* verticalScrollBar() const noexcept { return vbar_; }
    static constexpr int kHBarSteps = 1 << 30;
    int scrollY() const noexcept { return scrollY_; }
    int contentHeight() const; ///< sum of the row heights
    int rowsBottom() const;    ///< y below the last visible pixel of the rows (above the horizontal bar)

    // ---- Follow (stay at the end of a growing run, like tail -f) ----------------------------------
    /// While on, the right edge of the view is the end of the recorded data, with the current zoom width,
    /// every time dataChanged() reports more data. Turned on: the view jumps to the end at once. (Follow set
    /// before any data exists, from the saved setting, and no zoom chosen yet: the view keeps fitting the
    /// whole run, which ends at the end of the data too, until the first zoom fixes a width.) Turned off by
    /// anything that moves the window by hand: the horizontal bar, drag (middle button) or wheel panning,
    /// Ctrl+Left/Right, Home/End, go to a time, the view moving to show a cursor, zoom to fit and setView().
    /// Zooming (wheel, +/-, toolbar) keeps it on and keeps the right edge pinned instead of the point under
    /// the pointer. Changing the window width keeps the right edge pinned too.
    bool follow() const noexcept { return follow_; }
    void setFollow(bool on);

    // ---- Live data (a simulation that is still running) -----------------------------------------
    /// Call after a live source appended to the store: refreshes the rows and the view. Until the user
    /// zooms or pans, the view keeps fitting the whole run; afterwards it only follows the end if it was
    /// showing the end.
    void dataChanged();
    bool autoFit() const noexcept { return autoFit_; }
    /// What `add wave` does: adds the matching signals (patterns may name a dataset: `ref:/tb/*`), an
    /// expression row (-expr) or compare rows (-compare). Returns how many rows were added; when none,
    /// lastEditError() may say why.
    std::size_t addWaveSignals(const lvd::wave::AddWaveRequest& request);
    void clearWave(); ///< removes all rows (`delete wave *`)
    /// The rows as a script of `add wave` commands (see lvd::wave::exportDoScript).
    std::string doScript() const;
    /// Applies the `add wave` commands of a script. Needs a store, otherwise nothing is added.
    lvd::wave::ImportResult loadDoScript(const std::string& script);

    // ---- Datasets: other waveforms shown next to the current one ----------------------------------
    /// Adds (or replaces) a dataset: a finished waveform whose signals are named "name:path" in rows,
    /// commands and expressions (see lvd::wave::WaveSpace). Its time unit must be the current waveform's
    /// (convert with lvd::wave::retimeStore first). Returns an error message, empty on success. The rows are
    /// rebuilt.
    QString addDataset(const QString& name, std::shared_ptr<const lvd::wave::WaveStore> store);
    /// Removes a dataset; rows that show its signals stay and are drawn as missing.
    bool removeDataset(const QString& name);
    QStringList datasetNames() const;
    const lvd::wave::WaveSpace& space() const noexcept { return space_; }

    /// The data behind a row: the signal of a signal row (of any dataset), the result of an expression row,
    /// the difference signal of a compare row (per-bit rows: the vector); nullptr for buses, groups and
    /// dividers.
    const lvd::wave::Signal* rowSignal(int row) const;
    /// Why a row shows no data (an expression that does not compile, a signal that is not in the waveform),
    /// empty when it shows data.
    QString rowError(int row) const;

    // ---- Expression signals (rows computed from other signals) ------------------------------------
    /// Compiles an expression against the current waveform and the datasets: an error message (with the
    /// column), or empty and `description` set to the result type ("8 bit vector"). Syntax:
    /// lvd::wave::Expression.
    QString checkExpression(const QString& text, QString* description = nullptr) const;
    /// Inserts an expression row below the last selected row (at the end without a selection) and selects it.
    /// Refused (lastEditError()) if the expression does not compile or the name is empty. One undo step.
    bool addExpression(const QString& name, const QString& text,
                       lvd::wave::Radix radix = lvd::wave::Radix::Default);
    /// Changes the name and text of an expression row. One undo step.
    bool editExpression(lvd::wave::ItemId id, const QString& name, const QString& text);
    /// The "Expression Signal" dialog: a new row (edit == 0) or the expression row `edit`. Returns true when
    /// a row was added or changed.
    bool showExpressionDialog(lvd::wave::ItemId edit = 0);
    void setExpressionDialogRunner(std::function<bool(QDialog&)> runner) { exprRunner_ = std::move(runner); }

    // ---- Compare ---------------------------------------------------------------------------------
    /// Below every selected signal row of the current waveform: the same signal of `dataset` and a compare
    /// row (reference = dataset, test = current). Returns the number of compare rows added. One undo step.
    std::size_t compareSelectionWith(const QString& dataset, lvd::wave::Time tolerance = 0);
    /// Below every selected signal row: the same signal of `dataset`. Returns the number of rows added.
    std::size_t addDatasetRowsForSelection(const QString& dataset);
    /// Compares two whole waveforms ("" is the current one) below a scope path ("tb.dut", empty for all) and
    /// adds a group with a compare row for each signal that differs (at most maxRows). One undo step.
    lvd::wave::StoreDiff compareDatasets(const QString& ref, const QString& test, lvd::wave::Time tolerance,
                                         const QString& scope = {}, std::size_t maxRows = 200);
    /// The "Compare Waveforms" dialog (reference, test, scope, tolerance), then compareDatasets.
    bool showCompareDialog();
    void setCompareDialogRunner(std::function<bool(QDialog&)> runner) { compareRunner_ = std::move(runner); }
    /// Moves the active cursor to the start of the next (dir > 0) or previous difference of the selected
    /// compare rows (all compare rows if none is selected) and scrolls it into view; navigationMessage() says
    /// where and how many differences there are. False if there is none.
    bool jumpToDifference(int dir);
    /// Number of differences over all compare rows (each compare row counted once).
    std::size_t differenceCount() const;
    int compareRowCount() const;

    // ---- Rendering backend ---------------------------------------------------------------------
    enum class Backend { Cpu, Gpu };
    /// Cpu draws the wave area with QPainter. Gpu draws it with the GPU through Qt's rendering hardware
    /// interface (cached tiles stay on the GPU). Gpu is a request: if the window cannot render with the GPU,
    /// the widget falls back to Cpu on its own and renderBackendChanged() is emitted. The default comes from
    /// nothing: a bare widget starts on Cpu; the applications choose with resolveRenderPolicy()
    /// (render_policy.hpp).
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
    /// Replaces all markers; they are sorted by time. Markers live in the band between the ruler and the
    /// first row; they never cover the rows. A click on an arrow moves cursor 1 there.
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
    /// Sets cursor 1 (created if there is none) to a time in femtoseconds and makes it active. The view keeps
    /// its zoom and scrolls only if the time is not visible.
    void showTimeFs(long long fs);

    // ---- Navigation ----------------------------------------------------------------------------
    /// Move the active cursor to the previous (dir < 0) or next (dir > 0) edge of the selected rows, or of
    /// all rows if nothing is selected, and scroll it into view (a scroll turns Follow off). The search runs
    /// in libwave (lvd::wave::findEdge): an edge is a time step at which the value differs from the step
    /// before, so repeated values and glitches inside one time step are skipped. Rising and Falling only look
    /// at single-bit rows (1 bit signals and the bit rows of an expanded vector). Returns false if there is
    /// none; navigationMessage() then says why. Keys: Left/Right and Tab/Shift+Tab with the current
    /// edgeKind().
    bool jumpToEdge(int dir);
    bool jumpToEdge(int dir, lvd::wave::EdgeKind kind);
    /// Which edges the toolbar buttons and the keys look for (Any by default).
    lvd::wave::EdgeKind edgeKind() const noexcept { return edgeKind_; }
    void setEdgeKind(lvd::wave::EdgeKind kind);

    // ---- Find value ----------------------------------------------------------------------------
    /// A value search over the selected rows ("Find value" dialog, Ctrl+F; F3 / Shift+F3 repeat it).
    struct FindSpec {
        lvd::wave::SearchCondition condition = lvd::wave::SearchCondition::Equals;
        QString value; ///< in each row's radix for Equals / NotEquals; a number for the crossings
    };
    struct FindResult {
        bool found = false;
        bool wrapped = false; ///< found only after starting over from the other end
        double time = 0.0;
        int row = -1;    ///< row it was found in
        QString message; ///< what happened, for a status line
    };
    /// Searches every selected row from the active cursor (from the start or the end if there is none) for
    /// the time the condition starts to hold (lvd::wave::findValue) and moves the active cursor to the
    /// nearest hit, scrolling it into view. With wrap, a search that finds nothing starts over from the other
    /// end. Rows whose value cannot be read in their radix (or crossings on non-numeric rows) are skipped and
    /// named in the message.
    FindResult findValue(const FindSpec& spec, int dir, bool wrap = false);
    /// Repeats the last findValue (F3 forward, Shift+F3 backward). Nothing found if there was none.
    FindResult findAgain(int dir);
    /// The rows a search looks at: the selected rows that show data (never all rows).
    std::vector<int> searchRows() const;
    lvd::wave::Radix rowRadix(int row) const; ///< the radix a value is written in for this row
    bool rowIsNumeric(int row) const;         ///< threshold crossings work on this row
    /// Opens (or raises) the non-modal "Find Value" dialog; it belongs to this widget.
    FindValueDialog* showFindDialog();
    FindValueDialog* findDialog() const noexcept { return findDialog_; }
    void goToStart();           ///< cursor and view to the beginning of the simulation
    void goToEnd();             ///< cursor to the end, view scrolled so that it is visible
    void goToTime(double time); ///< cursor to a time (timescale units), view centred on it
    /// Moves the active cursor (cursor 1 if there is none) to t and scrolls only if t is not visible (the
    /// zoom is kept; a scroll turns Follow off). Used by edge and value searches and by the List window.
    void moveActiveCursorTo(double t);

    // ---- Rows and selection --------------------------------------------------------------------
    int rowCount() const noexcept { return static_cast<int>(rows_.size()); }
    std::string rowName(int row) const;
    std::string rowValueText(int row) const; ///< value at the active cursor, empty if none, "-" before the
                                             ///< first change and after the end of the data
    WaveClass rowClass(int row) const;       ///< valid only if rowHasSource(row)
    bool rowHasSource(int row) const;
    int rowBit(int row) const;         ///< bit index for per-bit rows, -1 otherwise
    bool rowExpandable(int row) const; ///< multi-bit signal that can show its bits
    std::optional<QColor> rowColor(int row) const;
    void selectRow(int row, bool additive = false);
    void selectRange(int fromRow, int toRow);
    void clearSelection();
    int selectedCount() const noexcept { return static_cast<int>(selected_.size()); }

    // ---- Arranging rows: move, group, rename, undo ----------------------------------------------
    // Every call below is one undo step when it changes something, keeps the selection (grouping selects the
    // new group, ungrouping selects the former children), scrolls so that the moved rows are visible and
    // emits structureChanged(). A refused edit changes nothing, sets lastEditError() and emits
    // editRejected().
    using ItemId = lvd::wave::ItemId;
    static constexpr std::size_t kAppend = static_cast<std::size_t>(-1); ///< index meaning "at the end"

    /// Ids of the selected whole items (per-bit rows are not items), in selection order.
    std::vector<ItemId> selectedItemIds() const;
    ItemId itemIdAtRow(int row) const; ///< 0 if out of range
    int rowOfItem(ItemId id) const;    ///< first row of the item, -1 if it is not shown (collapsed parent)
    bool isRowSelected(int row) const;
    /// Moves the selected items by |delta| places among their siblings (negative: up). Each stays in its
    /// group.
    bool moveSelectedRows(int delta);
    bool moveSelectedRowsToEdge(bool toTop); ///< top or bottom of the own group
    /// Moves items into a group (lvd::wave::kTopLevel for the top level) at an index of its child list
    /// (counted before the move, kAppend for the end). Items may come from different groups.
    bool moveRowsToGroup(const std::vector<ItemId>& ids, ItemId parent, std::size_t index = kAppend);
    bool moveSelectedToGroup(ItemId parent); ///< moveRowsToGroup(selection, parent, kAppend)
    /// Wraps the selected items in a new group (default name "Group", made unique) where the first one was.
    bool groupSelected(const std::string& label = {});
    bool ungroupSelected(); ///< replaces the selected groups by their children
    /// Renames a group, divider, bus or signal label. Groups and buses need a name; see
    /// lvd::wave::renameItem.
    bool renameItem(ItemId id, const std::string& label);
    /// Inserts a divider above the first / below the last selected item (top / end if nothing is selected)
    /// and selects it.
    bool insertDividerAbove(const std::string& label = {});
    bool insertDividerBelow(const std::string& label = {});
    QString lastEditError() const { return lastEditError_; }

    // Undo and redo of the edits above, and of removing rows, making buses and changing radix, colour or
    // analog display. Expanded/collapsed state is not part of the history. Bounded to 100 steps; adding
    // signals from the simulator or loading a layout starts a new history.
    bool canUndo() const noexcept { return !undo_.empty(); }
    bool canRedo() const noexcept { return !redo_.empty(); }
    QString undoText() const; ///< name of the edit undo() would revert, empty if none
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

    // ---- Analog settings -----------------------------------------------------------------------
    /// True if at least one selected row is an analog trace (what "Analog Settings..." applies to).
    bool analogSelectionAvailable() const;
    /// The range an analog row is drawn with (auto: the extent of the whole signal), empty if the row is not
    /// an analog trace or has no data yet.
    std::optional<lvd::wave::AnalogRange> effectiveAnalogRange(int row) const;
    /// Sets the range of every selected analog trace: automatic, or manual limits (refused when min >= max or
    /// not finite). One undo step. Returns false and sets lastEditError() when nothing was changed.
    bool setAnalogRangeForSelection(bool autoRange, double min = 0.0, double max = 1.0);
    /// Opens the modal "Analog Settings" dialog for the selection, preloaded from the clicked row (or the
    /// first selected analog row), and applies it on OK. Returns true if the settings were applied.
    bool showAnalogSettings(int row = -1);
    /// What shows the dialog; replaced by tests (fill the fields and call accept() instead of exec()). The
    /// default is `dialog.exec() == QDialog::Accepted`.
    using DialogRunner = std::function<bool(QDialog& dialog)>;
    void setAnalogDialogRunner(DialogRunner runner) { analogRunner_ = std::move(runner); }

    // ---- Row heights ---------------------------------------------------------------------------
    // Every row has a height in pixels (DisplayItem::heightPx, 0 = default). Set with "Height..." (dialog),
    // by dragging the bottom border of a row in the name or value column, or Alt+Up / Alt+Down. Per-bit rows
    // of an expanded vector keep the default height. Each change is one undo step and is saved with the
    // layout.
    /// Height of a row in pixels as drawn.
    int rowHeightAt(int row) const;
    /// True if a selected whole row can get a height (anything but per-bit rows).
    bool heightSelectionAvailable() const;
    /// Sets the height of every selected row: pixels (clamped to kMinRowHeightPx..kMaxRowHeightPx), or 0 for
    /// the default. One undo step. False and lastEditError() when nothing changed.
    bool setRowHeightForSelection(int px);
    /// Makes every selected row `delta` pixels taller (negative: shorter), from the height it is drawn with.
    bool adjustRowHeightForSelection(int delta);
    /// Opens the modal "Row Height" dialog, preloaded with the clicked row's height (or the first selected
    /// row's), and applies it on OK ("Default" resets). Returns true if something changed.
    bool showRowHeightDialog(int row = -1);
    void setRowHeightDialogRunner(DialogRunner runner) { heightRunner_ = std::move(runner); }
    /// The row whose bottom border is under pos (within 3 pixels, name or value column), -1 if none.
    int rowBorderAt(const QPoint& pos) const;
    /// Resizing a row with the mouse; the mouse handlers use these, tests may call them directly. The height
    /// follows the pointer live; finishing makes it one undo step, cancelling (Esc) restores the old height.
    bool beginRowResize(int row, int y);
    void updateRowResize(int y);
    bool finishRowResize();
    void cancelRowResize();
    bool rowResizeActive() const noexcept { return drag_ == Drag::RowHeight; }

    /// The context menu for a click on `row` (-1: not on a row), built from the current selection. The caller
    /// owns it. Actions have object names (moveUp, moveDown, moveTop, moveBottom, moveToGroupMenu,
    /// moveToTopLevel, moveToNewGroup, moveToGroup_<id>, groupSelected, ungroup, rename, insertDividerAbove,
    /// insertDividerBelow, delete, undo, redo, ...).
    QMenu* createContextMenu(int row);
    /// View Declaration in the context menu of signal rows (object name viewDeclaration): the gate returns
    /// why the declaration of a signal (store path) cannot be shown, empty when it can. Without a gate the
    /// entry is not offered (a bare widget has no simulator to ask).
    void setDeclarationGate(std::function<QString(const QString& path)> gate) {
        declarationGate_ = std::move(gate);
    }
    /// Adds the actions for expression signals, datasets and compare rows (row: the clicked row, -1 for
    /// none). Object names: addExpression, editExpression, addFromDatasetMenu, addFromDataset_<name>,
    /// compareWithMenu, compareWith_<name>, compareWaveforms, nextDifference, previousDifference.
    void populateDerivedMenu(QMenu* menu, int row = -1);
    /// Adds the arranging actions (move, group, rename, divider, delete, undo and redo) for the current
    /// selection.
    void populateArrangeMenu(QMenu* menu, bool withAnalogSettings = true);
    /// Replaces the dialog that asks for a group name; for tests. Returns the text or nothing if cancelled.
    using TextPrompt = std::function<std::optional<QString>(const QString& title, const QString& label,
                                                            const QString& initial)>;
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
    bool startRowDrag();                                 ///< drags the selected items; false if none
    RowDropTarget dropTargetAt(const QPoint& pos) const; ///< what a drop at pos would do
    void updateRowDrag(const QPoint& pos);
    bool finishRowDrag(const QPoint& pos); ///< drops; true if the rows were moved
    void cancelRowDrag();
    bool rowDragActive() const noexcept { return drag_ == Drag::Rows; }
    const RowDropTarget& rowDropIndicator() const noexcept { return dropTarget_; }
    /// One step of the scrolling that runs while a drag is near the top or bottom edge. Returns the scroll
    /// distance in pixels (0 when the pointer is not near an edge).
    int autoScrollStep();

    // Signals dragged in from the Objects pane (mime type kSignalPathsMime, see wave/signal_mime.hpp). The
    // wave widget accepts the drop on the name column, the value column and the wave area; the drop line and
    // the "into group" highlight are the ones of the row drag. Dropping adds the signals as `add wave
    // PATH...` does (radix, bus naming and bit handling of the display model; rows already shown are not
    // looked at, so a signal can appear twice) at the place under the pointer, or at the end below the last
    // row; a group row takes them in. The new rows are selected and the drop is one undo step. Tests may call
    // these directly or send drag events.
    /// What a signal drop at pos would do (valid = accepted, outside = not over the rows).
    RowDropTarget signalDropTargetAt(const QPoint& pos) const;
    bool signalDragActive() const noexcept;
    /// Adds the `add wave` paths ("/top/dut/clk") as rows of group `parent` (0: top level) at `index`
    /// (kAppend: end). Returns how many rows were added (0: nothing matched, nothing changed, editRejected is
    /// emitted).
    std::size_t dropSignals(const QStringList& addWavePaths, ItemId parent = 0, std::size_t index = kAppend);

    // ---- Editing the selection (each rebuilds the rows) -----------------------------------------
    void setRadixForSelection(lvd::wave::Radix radix);
    void toggleAnalogForSelection();
    void setColorForSelection(std::optional<QColor> color);
    bool groupSelection(const std::string& label = {}); ///< same as groupSelected()
    /// Build a virtual bus from the selected rows in display order (first is the most significant bit
    /// or member). Per-bit rows contribute single bits, signal rows the whole signal. Needs two or more
    /// bits in total. The bus is inserted after the last selected row.
    bool busFromSelection(const std::string& label = {});
    void removeSelection();
    void toggleGroup(int row);
    void toggleBits(int row);
    /// Insert a divider after the last selected row (at the end if nothing is selected).
    void insertDivider(const std::string& label); ///< same as insertDividerBelow()
    void promptInsertDivider();                   ///< asks for the label, then insertDivider

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
    /// The tree changed: rows were moved, grouped, renamed, removed or added. For marking a layout as
    /// modified.
    void structureChanged();
    /// An edit was refused (message for the status bar or transcript).
    void editRejected(const QString& message);
    void undoStateChanged();
    /// Follow was switched on or off (by setFollow or by a manual pan).
    void followChanged(bool on);
    void edgeKindChanged(lvd::wave::EdgeKind kind);
    /// A navigation or search result for the status bar ("No rising edge after 40 ns on the selected rows").
    void navigationMessage(const QString& message);
    /// Datasets were added or removed.
    void datasetsChanged();
    /// View Declaration for the signal (store path) of a row.
    void declarationRequested(const QString& path);

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
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dragLeaveEvent(QDragLeaveEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    friend class WaveOverlay;
    static constexpr int kRulerHeight = 40;
    static constexpr int kBandHeight = 18;
    static constexpr int kRowsTop = kRulerHeight + kBandHeight;
    static constexpr int kRowHeight = 22;

    struct Row {
        lvd::wave::DisplayItem* item = nullptr;
        int bit = -1; ///< >= 0 for the per-bit rows of an expanded vector (item is the vector)
        int depth = 0;
        int y = 0; ///< top in content coordinates (below the ruler)
        int h = kRowHeight;
        bool expandable = false;
        std::shared_ptr<RowSource> source;
        RowStyle style;
        std::uint64_t revision = 0;
        const void* cacheKey = nullptr;
        const lvd::wave::Signal* signal = nullptr; ///< the data of signal, expression and compare rows
        QString error;                             ///< why there is no data
        /// End of the waveform(s) the row's data comes from: the last value is held up to here only.
        Time dataEnd = std::numeric_limits<Time>::max();
    };

    /// A selected row: an item by id (survives every edit) and the bit of a per-bit row, -1 for the item
    /// itself.
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
    bool runEdit(const QString& text, const EditFn& fn,
                 const std::function<void(const lvd::wave::EditOutcome&)>& select = {}, ItemId reveal = 0);
    void pushUndo(Snapshot before);
    void restore(Snapshot snap);
    void afterEdit(); ///< rebuild, prune the selection, tell the world
    void ensureRowVisible(int row);
    void positionEditor();
    void closeEditor();
    std::optional<QString> promptText(const QString& title, const QString& label, const QString& initial);
    void drawDropIndicator(QPainter& p) const;
    RowDropTarget targetAt(const QPoint& pos, bool external) const;
    void endSignalDrag();
    void addGroupTargets(QMenu* menu, const std::vector<ItemId>& ids);

    void rebuildRows();
    const lvd::wave::NumericIndex* numericIndexFor(const lvd::wave::Signal& signal, bool isSigned);
    /// The expression and compare results of an item, built on first use and kept while a row shows them.
    const lvd::wave::Signal* expressionSignal(const lvd::wave::DisplayItem& item, QString* error,
                                              Time* dataEnd = nullptr);
    const lvd::wave::Signal* compareSignal(const lvd::wave::DisplayItem& item, QString* error);
    const lvd::wave::SignalCompare* compareOf(const lvd::wave::DisplayItem& item) const;
    lvd::wave::Time compareUntil(const lvd::wave::DisplayItem& item) const;
    void dropDerivedCaches();  ///< expression, compare and index caches (a new store or dataset)
    void pruneDerivedCaches(); ///< entries no row used in the last rebuildRows
    void derivedRowsChanged(); ///< rebuild after the waveforms behind the rows changed
    /// Inserts items below the selected signal rows; `make` returns the items for one row.
    std::size_t insertBelowSelectedSignals(
        const QString& text,
        const std::function<std::vector<lvd::wave::DisplayItem>(const lvd::wave::DisplayItem&)>& make);
    /// Latest end time over the current waveform and the datasets.
    Time spanEnd() const;
    int rowAtY(int y) const; ///< widget y to row index, -1 if none
    bool isSelected(const Row& row) const;
    std::vector<lvd::wave::DisplayItem*> selectedItems() const; ///< whole items only, no per-bit rows
    void updateScrollBar();
    void updateHBar(); ///< range, page and value of the horizontal bar from the view and the store
    void layoutBars(); ///< geometry of both bars
    void hbarMoved(int value);
    void pinToEnd();         ///< Follow: the right edge of the view at the end of the data
    void viewPannedByUser(); ///< a manual horizontal move: leaves auto fit and Follow
    void fitAll();
    void clampView();
    void drawRuler(QPainter& p) const;
    void drawRow(QPainter& p, int rowIndex, int top);
    void drawBusLabels(QPainter& p, const Row& row, int top) const;
    /// Height of the band that holds a row's name, value and bus text: the whole row, except for analog
    /// traces (their first line).
    int labelBand(const Row& row) const;
    /// The widget font, smaller for rows too low for it (never below a legible minimum).
    QFont rowFont(int band) const;
    /// End of the recorded data (simulation time reached): nothing is drawn after it.
    Time dataEnd() const;
    /// Widget x of the first pixel column after the end of the data.
    int noDataLeft() const;
    QColor noDataTint() const;
    void drawCursors(QPainter& p) const;
    void drawEventMarkers(QPainter& p) const;
    double fsPerUnit() const; ///< femtoseconds in one timescale unit of the store (1 without a store)
    std::vector<EventGroup> eventGroupsBetween(double xFrom,
                                               double xTo) const; ///< x relative to the wave area
    void paintOverlay(QPainter& p) const; ///< bus value labels and cursors, painted above the GPU canvas
    void updateGpuGeometry();
    void fallBackToCpu();
    std::string nameOf(const Row& row) const;
    void emitViewChanged();
    int effectiveWaveRight() const;
    int cursorNear(int x) const;
    void ensureVisible(double t);
    std::optional<double> activeCursorTime() const;
    QString timeText(double t) const;

    std::shared_ptr<const lvd::wave::WaveStore> store_;
    lvd::wave::DisplayModel model_;
    Palette palette_; // must be declared before renderer_, which is constructed from it
    std::unique_ptr<IRenderer> renderer_;
    /// Enough for a few screens of tiles at 4K whatever the row heights (96 MB of images), at most 8192
    /// tiles.
    TileCache cache_{8192, std::size_t{96} << 20};
    ViewState view_;

    std::vector<Row> rows_;
    std::vector<RowKey> selected_;
    int anchorRow_ = -1;
    std::deque<Snapshot> undo_, redo_;
    QString lastEditError_;
    QLineEdit* editor_ = nullptr;
    ItemId renameId_ = 0;
    bool editorBusy_ = false; ///< the editor is being hidden or moved by us: ignore its focus loss
    TextPrompt prompt_;
    QTimer* autoScroll_ = nullptr;
    int pressRow_ = -1;
    QPoint pressPos_;
    bool rowPressPending_ = false; ///< a press on a row that may turn into a drag
    bool deferSelect_ =
        false; ///< the press was on a selected row: collapse the selection on release if no drag
    std::vector<ItemId> dragIds_;
    RowDropTarget dropTarget_;
    lvd::wave::WaveSpace space_; ///< store_ plus the datasets: resolves every path of the rows
    std::map<std::pair<const lvd::wave::Signal*, bool>, std::unique_ptr<lvd::wave::NumericIndex>> indexes_;
    std::map<std::string, std::shared_ptr<RowSource>> busCache_;
    std::map<std::pair<const lvd::wave::Signal*, int>, std::shared_ptr<RowSource>> bitCache_;
    struct ExprEntry {
        std::unique_ptr<lvd::wave::ExpressionSignal> signal;
        QString error;
        std::vector<std::string> datasets; ///< the waveforms its names come from ("" = current)
        bool used = false;
    };
    std::map<std::string, ExprEntry> exprCache_; ///< by expression text
    struct CompareEntry {
        std::unique_ptr<lvd::wave::SignalCompare> compare;
        QString error;
        bool used = false;
    };
    std::map<std::string, CompareEntry> compareCache_; ///< by reference, test and tolerance
    std::function<bool(QDialog&)> exprRunner_;
    std::function<QString(const QString&)> declarationGate_;
    std::function<bool(QDialog&)> compareRunner_;
    std::uint64_t storeRevision_ = 1;
    RhiWaveCanvas* canvas_ = nullptr;
    QWidget* overlay_ = nullptr;
    Backend requested_ = Backend::Cpu;
    bool gpuActive_ = false;
    QString fallbackReason_;
    bool autoFit_ = true;
    double lastEnd_ = 0.0;

    std::vector<EventMarker> events_; ///< sorted by time
    /// cumulative counts per severity: eventPrefix_[s][i] events among the first i with severity s
    std::vector<std::uint32_t> eventPrefix_[4];

    std::vector<double> cursors_;
    int activeCursor_ = 0;
    int nameWidth_ = 220;
    int valueWidth_ = 110;
    int scrollY_ = 0;
    QScrollBar* vbar_ = nullptr;
    QScrollBar* hbar_ = nullptr;
    bool hbarSync_ = false; ///< the bar is being set from the view: ignore its valueChanged
    bool follow_ = false;
    DialogRunner analogRunner_;
    lvd::wave::EdgeKind edgeKind_ = lvd::wave::EdgeKind::Any;
    std::optional<FindSpec> lastFind_;
    bool lastFindWrap_ = false;
    FindValueDialog* findDialog_ = nullptr;

    DialogRunner heightRunner_;
    ItemId resizeId_ = 0; ///< the row being resized with the mouse
    int resizeStartY_ = 0;
    int resizeStartH_ = 0;
    std::optional<Snapshot> resizeBefore_;

    enum class Drag {
        None,
        Cursor,
        Pan,
        NameSplitter,
        ValueSplitter,
        Rows,
        Signals,
        RowHeight
    } drag_ = Drag::None;
    int dragCursor_ = 0;
    QPoint lastMouse_;
};

} // namespace lvd::gui
