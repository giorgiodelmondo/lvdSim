// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvdwave/expression.hpp>
#include <lvdwave/list_index.hpp>
#include <lvdwave/radix.hpp>
#include <lvdwave/store.hpp>
#include <lvdwave/wave_space.hpp>

#include <QAbstractTableModel>
#include <QStringList>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QIODevice;

namespace lvd::gui {

/// When the list prints a row; the trigger signal is named by path so it survives a new store (restart).
struct ListRowMode {
    lvd::wave::ListTrigger trigger = lvd::wave::ListTrigger::AnyChange;
    std::string triggerPath;         ///< dotted store path, ListTrigger::Signal
    lvd::wave::Time strobeStart = 0; ///< timescale units, ListTrigger::Strobe
    lvd::wave::Time strobePeriod = 0;
    bool operator==(const ListRowMode&) const = default;
};

/// The table of the List window: column 0 is the time, then one column per listed signal, one row per time
/// step chosen by the row mode. Nothing is built per row: the row times come from a lvd::wave::ListIndex and
/// every cell is read from the store when the view asks for it (O(log changes) per cell), so a million rows
/// cost no more than a hundred. Values are printed in each column's radix; a cell whose signal changes at the
/// row's time is bold. Before a signal's first change the cell shows "-".
class ListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    struct Column {
        /// dotted store path ("tb.dut.q"), a dataset path ("ref:tb.dut.q") or an expression ("=a and b")
        std::string path;
        std::string label; ///< header text; empty: the path without the top scope
        lvd::wave::Radix radix = lvd::wave::Radix::Default;
    };

    explicit ListModel(QObject* parent = nullptr);

    /// A new store (a new simulation or a restart): the columns are kept and looked up again by path.
    void setStore(std::shared_ptr<const lvd::wave::WaveStore> store);
    /// Where dataset paths and the names inside expressions are looked up (the wave window's space). Without
    /// it only the store's own signals are known. The space must outlive the model or be reset with nullptr.
    void setSpace(const lvd::wave::WaveSpace* space);
    /// The datasets changed: look every column up again.
    void datasetsChanged() { rebuildIndex(); }
    const lvd::wave::WaveStore* store() const noexcept { return store_.get(); }
    /// The store grew (live run): new rows are inserted at the end, the last row is refreshed.
    void refresh();

    /// Appends columns for store paths; returns how many were added.
    int addSignals(const std::vector<std::string>& paths,
                   std::optional<lvd::wave::Radix> radix = std::nullopt, const std::string& label = {});
    const std::vector<Column>& signalColumns() const noexcept { return columns_; }
    /// Column arguments below count from the first signal column (= table column 1).
    void removeSignalColumn(int index);
    bool moveSignalColumn(int from, int to);
    void setRadix(int index, lvd::wave::Radix radix);
    void clearColumns();

    void setRowMode(const ListRowMode& mode);
    const ListRowMode& rowMode() const noexcept { return mode_; }

    /// Power of ten (seconds) of the unit the time column is printed in (-9: ns). Default: ns, or the
    /// timescale unit when that is coarser.
    int timeUnitExp() const noexcept { return unitExp_; }
    void setTimeUnitExp(int exp);
    QString timeHeader() const; ///< "Time (ns)"
    /// A time exactly in the time unit ("12.5", no rounding).
    QString timeValue(lvd::wave::Time t) const;

    lvd::wave::Time rowTime(int row) const;
    /// The last row at or before t, -1 if none.
    int rowAtOrBefore(lvd::wave::Time t) const;
    QString cellText(int row, int column) const;
    QString headerText(int column) const;

    /// Rows as text: a header line, then one line per row, fields separated by `sep` (tab for the clipboard).
    QString rowsText(const std::vector<int>& rows, QChar sep) const;
    /// Every row as CSV (UTF-8, RFC 4180 quoting, header "Time (ns)" and the signal paths). Returns false and
    /// sets error if writing fails.
    bool writeCsv(QIODevice& out, QString* error = nullptr) const;

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

signals:
    /// Columns or the row mode changed (for layout persistence and the trigger combo).
    void columnsChanged();

private:
    void rebuildIndex();
    void resolveIds();
    /// The data of a column path (see Column::path), nullptr if unknown. Expressions are built on first use.
    const lvd::wave::Signal* lookup(const std::string& path);

    std::shared_ptr<const lvd::wave::WaveStore> store_;
    const lvd::wave::WaveSpace* space_ = nullptr;
    std::vector<Column> columns_;
    std::vector<const lvd::wave::Signal*> ids_; ///< per column, nullptr when not found
    std::map<std::string, std::unique_ptr<lvd::wave::ExpressionSignal>> exprs_; ///< by expression text
    ListRowMode mode_;
    lvd::wave::ListIndex index_;
    int rows_ = 0;
    int unitExp_ = -9;
    bool unitChosen_ = false;
};

} // namespace lvd::gui
