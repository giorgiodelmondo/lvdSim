// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "wave/list_model.hpp"

#include <lvdwave/wave_commands.hpp>

#include <QWidget>

#include <functional>
#include <memory>
#include <optional>

class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QTableView;
class QToolButton;

namespace lvd::gui {

/// The List window: signal values as a table, one row per time step (see ListModel), for a dock of the
/// simulation window. Rows come from any change of the listed signals, the changes of one trigger signal or a
/// strobe interval (the "Rows" box above the table).
///
/// - Signals are added with `add list` (addList), by dropping them from the Objects pane (the wave
///   window's drag format, see signal_mime.hpp) or with addPaths(). The header's context menu changes a
///   column's radix, moves it left or right or removes it (also Delete); headers can be dragged to reorder.
/// - Selecting a row emits timeSelected(), which the simulation window uses to move the wave cursor;
///   showTime() selects the row of a time without emitting it back.
/// - Ctrl+C copies the selected rows (tab separated, with a header line); "Export CSV..." writes every row.
/// - While a simulation runs, refresh() appends the new rows; the view stays at the end if it was there.
///
/// Object names, for tests: listTable, rowModeBox, triggerBox, strobeEdit, strobeStartEdit, exportCsv,
/// rowCountLabel.
class ListWindow : public QWidget {
    Q_OBJECT
public:
    explicit ListWindow(QWidget* parent = nullptr);

    ListModel* model() const noexcept { return model_; }
    QTableView* table() const noexcept { return table_; }

    void setStore(std::shared_ptr<const lvd::wave::WaveStore> store);
    void refresh();

    /// What `add list` does: the request's patterns are expanded like `add wave` patterns (-r, -radix, -label
    /// are used). Returns the number of columns added.
    int addList(const lvd::wave::AddWaveRequest& request);
    /// Dataset paths (`add list ref:/tb/*`) and the names in expression columns (`add list -expr {a and b}`)
    /// are looked up here (the wave window's space). nullptr: the store alone.
    void setSpace(const lvd::wave::WaveSpace* space) {
        space_ = space;
        model_->setSpace(space);
    }
    /// `add wave` style paths ("/top/dut/clk"), as dropped from the Objects pane.
    int addPaths(const QStringList& addWavePaths);
    void clear(); ///< `delete list *`

    /// Selects the row at or before t (timescale units) and scrolls to it, without emitting timeSelected().
    void showTime(double t);
    /// Copies the selected rows to the clipboard; returns the text.
    QString copySelection();
    bool exportCsv(const QString& path, QString* error = nullptr);
    /// Asks for a file name (replaceable for tests), then exportCsv(). Returns true if a file was written.
    bool exportCsvDialog();
    using FilePrompt = std::function<QString()>;
    void setFilePrompt(FilePrompt prompt) { filePrompt_ = std::move(prompt); }

    /// The header context menu for a table column (0 is the time); the caller owns it.
    QMenu* createHeaderMenu(int column);
    /// View Declaration in the header menu of signal columns (object name viewDeclaration), as in the wave
    /// window: the gate returns why the declaration of a store path cannot be shown, empty when it can.
    /// Without a gate the entry is not offered.
    void setDeclarationGate(std::function<QString(const QString& path)> gate) {
        declarationGate_ = std::move(gate);
    }

signals:
    /// The user selected a row: the cursor should go to this time (timescale units).
    void timeSelected(double time);
    /// Columns were added (for raising the dock).
    void signalsAdded(int count);
    /// View Declaration for the signal (store path) of a column.
    void declarationRequested(const QString& path);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateRowModeUi();
    void applyRowMode();
    void updateCount();

    ListModel* model_ = nullptr;
    const lvd::wave::WaveSpace* space_ = nullptr;
    QTableView* table_ = nullptr;
    QComboBox* rowMode_ = nullptr;
    QComboBox* trigger_ = nullptr;
    QLineEdit* strobe_ = nullptr;
    QLineEdit* strobeStart_ = nullptr;
    QLabel* strobeStartLabel_ = nullptr;
    QLabel* count_ = nullptr;
    FilePrompt filePrompt_;
    std::function<QString(const QString&)> declarationGate_;
    bool syncing_ = false; ///< selecting a row from showTime(): do not emit timeSelected
    bool updatingUi_ = false;
};

} // namespace lvd::gui
