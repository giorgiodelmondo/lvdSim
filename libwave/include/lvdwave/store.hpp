// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvdwave/logic.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lvd::wave {

using Time = std::uint64_t;  ///< Simulation time in timescale units (femtoseconds for NVC dumps).
using SignalId = std::uint32_t;
using ScopeId = std::uint32_t;

inline constexpr SignalId kNoSignal = 0xFFFFFFFFu;
inline constexpr ScopeId kRootScope = 0;
inline constexpr ScopeId kNoScope = 0xFFFFFFFFu;
inline constexpr std::size_t kNoIndex = static_cast<std::size_t>(-1);

enum class SignalKind : std::uint8_t {
    Logic,   ///< bit or bit vector of nine-valued logic; integers and enums-as-bits also land here
    Real,    ///< IEEE double
    String,  ///< variable length text (VHDL enumerations, physical types, strings)
};

/// All value changes of one signal, in non-decreasing time order.
/// Several changes may share a time (delta cycles); the last one at a time wins for lookups.
class Signal {
public:
    Signal(std::string name, ScopeId scope, SignalKind kind, std::uint32_t width, SignalId aliasOf = kNoSignal);

    const std::string& name() const noexcept { return name_; }
    ScopeId scope() const noexcept { return scope_; }
    SignalKind kind() const noexcept { return kind_; }
    /// Number of bits for Logic signals, 1 for the others.
    std::uint32_t width() const noexcept { return width_; }
    /// Target signal when this entry is an alias, else kNoSignal.
    SignalId aliasOf() const noexcept { return aliasOf_; }

    std::size_t changeCount() const noexcept { return times_.size(); }
    Time time(std::size_t change) const noexcept { return times_[change]; }

    /// Index of the change in effect at time t (the last change with time <= t), or kNoIndex.
    /// O(log n).
    std::size_t holdIndex(Time t) const noexcept;

    /// Half-open index range [first, last) of changes with t0 <= time < t1. O(log n).
    std::pair<std::size_t, std::size_t> changesIn(Time t0, Time t1) const noexcept;

    /// One bit of a Logic change; bitIndex 0 is the least significant bit. O(1).
    Logic bit(std::size_t change, std::uint32_t bitIndex) const noexcept;
    /// Whole Logic value, most significant bit first. O(width).
    std::string bits(std::size_t change) const;
    double real(std::size_t change) const noexcept { return reals_[change]; }
    std::string_view text(std::size_t change) const noexcept { return table_[textIds_[change]]; }

    /// Appending. Times must not decrease. Values shorter than the width are left-extended
    /// (with 0, or with X/Z/U when the leading character is one of those), longer ones are truncated.
    void appendLogic(Time t, std::string_view msbFirst);
    void appendReal(Time t, double v);
    void appendText(Time t, std::string_view s);

    /// Indices of the changes at which one bit of a Logic signal differs from the previous change
    /// (the first change is always included). O(n). Empty for non-Logic signals or bit >= width.
    std::vector<std::size_t> bitChanges(std::uint32_t bitIndex) const;

    /// Approximate heap usage in bytes.
    std::size_t memoryBytes() const noexcept;

private:
    std::size_t bytesPerChange() const noexcept { return (static_cast<std::size_t>(width_) + 1) / 2; }
    void setNibble(std::size_t byteBase, std::uint32_t bitIndex, Logic v) noexcept;

    std::string name_;
    ScopeId scope_;
    SignalKind kind_;
    std::uint32_t width_;
    SignalId aliasOf_;

    std::vector<Time> times_;
    std::vector<std::uint8_t> scalar_;  // width == 1: one Logic code per change
    std::vector<std::uint8_t> packed_;  // width > 1: two Logic nibbles per byte, fixed stride
    std::vector<double> reals_;
    std::vector<std::uint32_t> textIds_;
    std::vector<std::string> table_;
    std::unordered_map<std::string, std::uint32_t> tableIndex_;
};

struct Scope {
    std::string name;
    ScopeId parent = kRootScope;
    int type = 0;  ///< Producer specific scope type code (FST scope type for FST files).
    std::vector<ScopeId> children;
    std::vector<SignalId> signalIds;
};

/// The loaded waveform database: scope tree, signals and their change lists.
class WaveStore {
public:
    WaveStore();

    ScopeId addScope(ScopeId parent, std::string name, int type = 0);
    SignalId addSignal(ScopeId scope, std::string name, SignalKind kind, std::uint32_t width);
    SignalId addAlias(ScopeId scope, std::string name, SignalId target);

    std::size_t scopeCount() const noexcept { return scopes_.size(); }
    std::size_t signalCount() const noexcept { return signals_.size(); }
    const Scope& scope(ScopeId id) const { return scopes_[id]; }

    /// The entry itself (aliases are separate entries with no data of their own).
    const Signal& entry(SignalId id) const { return signals_[id]; }
    /// The signal that holds the data for id, following an alias.
    const Signal& signal(SignalId id) const;
    Signal& mutableSignal(SignalId id);
    SignalId resolve(SignalId id) const noexcept;

    /// Dotted hierarchical path without the unnamed root, e.g. "top.dut.clk".
    std::string pathOf(SignalId id) const;
    std::string scopePath(ScopeId id) const;
    /// O(1) amortised after the first call. Returns kNoSignal if there is no such path.
    SignalId findSignal(std::string_view path) const;
    /// Scope with this dotted path ("tb.dut"), or kNoScope. An empty path is the unnamed root.
    ScopeId findScope(std::string_view path) const;

    Time startTime() const noexcept { return start_; }
    Time endTime() const noexcept { return end_; }
    void extendTimeRange(Time t) noexcept;

    int timescaleExponent() const noexcept { return timescaleExp_; }
    void setTimescaleExponent(int e) noexcept { timescaleExp_ = e; }

    std::uint64_t totalChanges() const noexcept;
    std::size_t memoryBytes() const noexcept;

private:
    std::vector<Scope> scopes_;
    std::deque<Signal> signals_;  // deque: references stay valid while a live source appends signals
    Time start_ = 0;
    Time end_ = 0;
    bool haveTime_ = false;
    int timescaleExp_ = -15;
    mutable std::unordered_map<std::string, SignalId> pathIndex_;
    mutable bool pathIndexValid_ = false;
    mutable std::unordered_map<std::string, ScopeId> scopeIndex_;
    mutable bool scopeIndexValid_ = false;
};

}  // namespace lvd::wave
