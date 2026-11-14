// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvdwave/expression.hpp>
#include <lvdwave/result.hpp>
#include <lvdwave/store.hpp>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace lvd::wave {

/// All waveforms the wave window can show: the current one (the live run or the opened file) and any number
/// of named datasets opened next to it (finished FST files, never one that is still being written).
///
/// Paths of dataset signals carry the dataset name as a prefix: "ref:tb.dut.q" in display items, and
/// "ref:/tb/dut/q" in commands and expressions. A path without a prefix belongs to the current waveform.
/// Dataset names are identifiers (a letter, then letters, digits and '_'), so the first ':' always ends the
/// prefix (VHDL and Verilog names contain no ':').
class WaveSpace {
public:
    struct Split {
        std::string_view dataset; ///< empty for the current waveform
        std::string_view path;
    };
    /// "ref:tb.x" -> {"ref", "tb.x"}; "tb.x" -> {"", "tb.x"}. A prefix that is not a valid name is not one.
    static Split split(std::string_view prefixed) noexcept;
    static std::string join(std::string_view dataset, std::string_view path);
    static bool validDatasetName(std::string_view name) noexcept;

    void setCurrent(std::shared_ptr<const WaveStore> store) { current_ = std::move(store); }
    const WaveStore* current() const noexcept { return current_.get(); }

    /// Adds or replaces a dataset. The store's time unit must match the current waveform's (see
    /// retimeStore); an empty string means success, otherwise the reason.
    std::string addDataset(const std::string& name, std::shared_ptr<const WaveStore> store);
    bool removeDataset(const std::string& name);
    std::vector<std::string> datasetNames() const;
    std::shared_ptr<const WaveStore> datasetShared(const std::string& name) const;

    /// The store of a dataset ("" = the current waveform), or nullptr.
    const WaveStore* store(std::string_view dataset) const;
    /// The store a prefixed path belongs to, or nullptr.
    const WaveStore* storeOf(std::string_view prefixedPath) const {
        return store(split(prefixedPath).dataset);
    }
    /// The data of a prefixed dotted path ("ref:tb.dut.q"), following aliases, or nullptr.
    const Signal* findPath(std::string_view prefixedPath) const;

    /// A name as written in an expression or a command: optional dataset prefix, then a path with '/'
    /// (`/tb/dut/q`, relative to the design top or starting with its name), a dotted path (`tb.dut.q`) or a
    /// bare name below the design top (`q`). Vector names may leave out the range NVC adds ("count" for
    /// "count[7:0]"). Wildcards are refused. Returns nullptr and sets error if there is not exactly one
    /// match.
    const Signal* findName(std::string_view name, std::string& error) const;
    /// findName as a resolver for expressions. The space must outlive it.
    SignalResolver resolver() const;

    /// Signals of a store matching an `add wave` pattern (with optional dataset prefix), as prefixed dotted
    /// paths.
    std::vector<std::string> expandPattern(std::string_view prefixedPattern, bool recursive) const;

    /// The latest end time over the current waveform and the datasets (0 without any).
    Time endTime() const noexcept;
    /// The end time of the store a path belongs to (0 if none).
    Time endTimeOf(std::string_view prefixedPath) const;

private:
    std::shared_ptr<const WaveStore> current_;
    std::map<std::string, std::shared_ptr<const WaveStore>, std::less<>> datasets_;
};

/// A copy of `src` with times in another unit (10^exponent seconds). To a finer unit times are multiplied
/// (refused if they would overflow); to a coarser one they are divided and rounded down, and changes that
/// land on one time become delta cycles of that time.
Result<WaveStore> retimeStore(const WaveStore& src, int exponent);

} // namespace lvd::wave
