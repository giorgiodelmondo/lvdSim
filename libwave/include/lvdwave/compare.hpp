// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvdwave/search.hpp>
#include <lvdwave/store.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lvd::wave {

/// Waveform comparison: where do a reference signal and a test signal hold different values?
///
/// Values are compared per time step (delta cycles at one time count as one step with the last value).
/// Logic values are equal when every bit is equal after zero extension to the wider width, with L equal to 0
/// and H equal to 1 (X, Z, U, W and - only equal themselves). Reals are equal within `realTolerance`, text
/// when identical. Before its first change a signal has no value, which equals only "no value".

struct CompareOptions {
    /// Differences that last this long or less are ignored, so an edge that moved by up to the tolerance is
    /// not reported. 0 reports every difference.
    Time tolerance = 0;
    double realTolerance = 0.0; ///< absolute
};

/// A reported difference: the values differ from `start` up to (not including) `end`.
struct DiffInterval {
    Time start = 0;
    Time end = 0;
    bool operator==(const DiffInterval&) const = default;
};

/// Do two signals hold the same value at changes ia and ib (kNoIndex: no value yet)? See the rules above.
bool sameValue(const Signal& a, std::size_t ia, const Signal& b, std::size_t ib, double realTolerance = 0.0);

/// The differences of one signal pair, computed incrementally: update(until) reads the changes up to `until`
/// (normally the end of the data both sides have), so a live run can be compared while it grows. The result
/// is also a one bit Signal (1 while a reported difference lasts), which the wave window draws as a normal
/// row and searches with findEdge. Cost: O(changes of both signals x width) over all calls together.
///
/// Both signals must outlive this object and may only grow between calls, or be cleared (Signal::clearChanges,
/// a restarted live run): then the next update() compares from the start again.
class SignalCompare {
public:
    SignalCompare(const Signal& ref, const Signal& test, CompareOptions options = {},
                  std::string name = "diff");

    /// Compares the time steps up to and including `until`; returns the number of differences reported by
    /// this call (a difference that is still going on counts once it is longer than the tolerance).
    std::size_t update(Time until);

    /// Finished differences in time order.
    const std::vector<DiffInterval>& intervals() const noexcept { return intervals_; }
    /// Start of a reported difference that is still going on at the last `until`, if any.
    std::optional<Time> openSince() const noexcept;
    /// Number of reported differences (finished and going on).
    std::size_t count() const noexcept { return intervals_.size() + (openSince() ? 1 : 0); }
    const Signal& differences() const noexcept { return out_; }
    const CompareOptions& options() const noexcept { return options_; }

private:
    void step(Time t);

    const Signal& ref_;
    const Signal& test_;
    CompareOptions options_;
    Signal out_;
    std::vector<DiffInterval> intervals_;
    std::size_t nextRef_ = 0, nextTest_ = 0;
    std::size_t curRef_ = kNoIndex, curTest_ = kNoIndex;
    bool started_ = false;
    bool differ_ = false;
    bool emitted_ = false; ///< the open difference has been written to out_
    Time open_ = 0;
    /// What the step at the last time did when it ended a difference, so that later delta cycles of that time
    /// which make the values differ again continue the difference instead of starting a new one.
    struct Close {
        Time open = 0;
        bool level = false;    ///< out_ showed the difference before it ended
        bool recorded = false; ///< it was added to intervals_
    };
    std::optional<Close> lastClose_;
    std::optional<Time> lastStep_;
    std::uint64_t generation_ = 0; ///< sum of the two signals' Signal::generation() at the last update
};

/// Start of the next (forward) or previous (backward) reported difference strictly after / before `from`
/// (everything without `from`), as findEdge finds edges.
std::optional<Time> findDifference(const SignalCompare& cmp, std::optional<Time> from, SearchDirection dir);

/// One signal that differs in a store comparison.
struct SignalDiff {
    std::string path;      ///< dotted path, the same in both stores
    std::size_t count = 0; ///< reported differences
    Time first = 0;        ///< start of the first one
};

struct StoreDiff {
    Time until = 0;                        ///< compared up to: the earlier of the two end times
    std::size_t compared = 0;              ///< signals present in both
    std::size_t total = 0;                 ///< differences over all signals
    std::vector<SignalDiff> differing;     ///< in the test store's order
    std::vector<std::string> onlyInRef;    ///< paths missing in the test store
    std::vector<std::string> onlyInTest;   ///< paths missing in the reference store
    std::vector<std::string> kindMismatch; ///< same path, different kind or width
};

/// Compares every signal of `test` whose dotted path starts with `scope` (all if empty) with the signal of
/// the same path in `ref`, up to the earlier end time. Signals reached through several paths (ports) are
/// compared once, under their first path. O(total changes of the compared signals).
StoreDiff compareStores(const WaveStore& ref, const WaveStore& test, const CompareOptions& options = {},
                        std::string_view scope = {});

/// Parses a duration such as "2 ns", "2ns", "1.5us", "100" (bare numbers are in store units) into units of
/// 10^exponent seconds. Nothing for text that is not a non-negative duration.
std::optional<Time> parseDuration(std::string_view text, int exponent);

} // namespace lvd::wave
