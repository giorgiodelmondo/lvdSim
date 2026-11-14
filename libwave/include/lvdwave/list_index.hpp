// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvdwave/store.hpp>

#include <cstddef>
#include <vector>

namespace lvd::wave {

/// When the list window prints a row.
enum class ListTrigger : std::uint8_t {
    AnyChange, ///< at every time step in which any listed signal changes
    Signal,    ///< at every time step in which the trigger signal changes
    Strobe,    ///< every `strobePeriod` from `strobeStart` up to the end of the data
};

struct ListOptions {
    ListTrigger trigger = ListTrigger::AnyChange;
    SignalId triggerSignal = kNoSignal; ///< ListTrigger::Signal
    Time strobeStart = 0;               ///< ListTrigger::Strobe
    Time strobePeriod = 0;              ///< ListTrigger::Strobe, must be > 0
};

/// The rows of a list window: one per time step (delta cycles at one time are one row, showing the last
/// value), without building them. Change-driven rows are found by merging the change lists of the signals;
/// every kCheckpointEvery rows the merge position is remembered, so row r is reached from the checkpoint
/// before it.
///
/// Costs: update() is O(new changes x signals); rowTime() is O(1) for the next row after the last one asked
/// for and O(kCheckpointEvery x signals) at worst for a random row; rowAtOrBefore() adds a binary search.
/// Memory is (rows / kCheckpointEvery) x signals x 8 bytes. Strobe rows are computed (O(1)).
///
/// The store must outlive the index. Not thread safe (rowTime keeps a cursor).
class ListIndex {
public:
    static constexpr std::size_t kCheckpointEvery = 1024;

    ListIndex() = default;
    ListIndex(const WaveStore& store, std::vector<SignalId> columns, ListOptions options = {});
    /// The same over signals that need not be entries of the store (dataset or expression signals); `trigger`
    /// replaces options.triggerSignal. The store gives the end time for strobe rows. All signals must outlive
    /// the index.
    ListIndex(const WaveStore& store, std::vector<const Signal*> columns, const Signal* trigger,
              ListOptions options = {});

    /// Takes in the changes appended to the store since the last call (all of them the first time).
    /// Returns the number of rows added. After a source signal was cleared (Signal::clearChanges, a restarted
    /// live run) the rows are built again from the start.
    std::size_t update();

    std::size_t rowCount() const noexcept { return rows_; }
    /// Time of a row (row < rowCount()).
    Time rowTime(std::size_t row) const;
    /// The last row whose time is <= t, or kNoIndex if there is none.
    std::size_t rowAtOrBefore(Time t) const;

    const ListOptions& options() const noexcept { return options_; }

private:
    struct Cursor {
        std::size_t row = 0;          ///< the row the next step produces
        std::vector<std::size_t> pos; ///< next unread change per source
    };
    bool nextTime(const std::vector<std::size_t>& pos, Time& t) const;
    void consume(std::vector<std::size_t>& pos, Time t) const;
    void seek(std::size_t row) const; ///< cursor_ to `row`

    const WaveStore* store_ = nullptr;
    ListOptions options_;
    std::vector<const Signal*> sources_;
    std::size_t rows_ = 0;
    // Change-driven rows
    std::vector<std::size_t> frontier_; ///< merge position after the last row
    Time lastTime_ = 0;
    std::vector<std::size_t> checkpoints_; ///< flattened: positions before row k * kCheckpointEvery
    std::vector<Time> checkpointTimes_;    ///< time of row k * kCheckpointEvery
    mutable Cursor cursor_;
    mutable bool cursorValid_ = false;
    std::uint64_t generation_ = 0; ///< sum of the sources' Signal::generation() at the last update
};

} // namespace lvd::wave
