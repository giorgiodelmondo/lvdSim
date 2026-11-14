// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvdwave/display_model.hpp>
#include <lvdwave/result.hpp>
#include <lvdwave/store.hpp>
#include <lvdwave/wave_space.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lvd::wave {

/// Splits a Tcl list ("a {b c} \"d e\" f\\ g") into its elements. Braces nest, double quotes group,
/// a backslash escapes the next character. Unbalanced input is returned as far as it could be read.
std::vector<std::string> parseTclList(std::string_view text);

/// A parsed `add wave` command.
struct AddWaveRequest {
    std::vector<std::string> patterns; ///< hierarchical paths, e.g. /tb/dut/*
    bool recursive = false;            ///< -r: also signals in scopes below the matching ones
    std::optional<Radix> radix;
    std::optional<std::uint32_t> color; ///< 0xRRGGBB
    std::string label;                  ///< -label, used when exactly one signal is added
    std::string group;                  ///< -group: put the signals into a group of that name
    std::optional<std::string> divider; ///< -divider: add a divider instead of signals
    bool analog = false;                ///< -format analog...
    std::optional<double> min, max;     ///< -min / -max for the analog range
    int heightRows = 0;                 ///< -height in rows, 0 if not given
    int heightPx = 0;                   ///< -height in pixels (clamped, see clampRowHeight), 0 if not given
    bool linear = false;                ///< -format analog-interpolated
    std::optional<std::string> vbus;    ///< -vbus: one virtual bus with this label; patterns may end in @bit
    bool collapse = false;              ///< -collapse: a group created by this command starts collapsed
    bool showBits = false;              ///< -bits: show one row per bit under vector signals
    std::optional<std::string> expr;    ///< -expr TEXT: one expression row (label from -label, else the text)
    bool compare = false;               ///< -compare REF TEST: compare rows of the signal pairs
    std::string tolerance;              ///< -tolerance T for -compare ("2ns", "2 ns", or store units)
};

/// Parses the words after `add wave`: options -r/-recursive, -radix R, -hex -binary -octal -decimal
/// -unsigned -signed -ascii, -color C (name or #rrggbb), -label L, -group G, -divider T,
/// -format analog*, -min, -max, -height PIXELS (the row height of any row), -expr TEXT, -compare, -tolerance
/// T; anything else that starts with
/// '-' is skipped as a flag.
Result<AddWaveRequest> parseAddWave(const std::vector<std::string>& words);

/// Signals of the store matching a path pattern, as dotted store paths in scope order.
///
/// Paths use '/' as separator and are relative to the design top, as in the NVC shell (`/cnt`), or may
/// start with the top's name as common simulator scripts do (`/tb/dut/clk`). `*` matches any characters and
/// `?` one, case-insensitively (VHDL). With recursive set, the last component also matches in all scopes
/// below the ones it was found in.
std::vector<std::string> expandWavePattern(const WaveStore& store, const std::string& pattern,
                                           bool recursive);

/// Adds what the request describes to the display model (at the end, or inside the requested group).
/// Signals already shown are added again, as in the familiar flow. Returns how many rows were added.
std::size_t applyAddWave(DisplayModel& model, const WaveStore& store, const AddWaveRequest& request);
/// The same over a WaveSpace: patterns may carry a dataset prefix (`ref:/tb/dut/*`), and the rows get
/// prefixed paths. `-expr` adds one expression row (not checked here: the wave window shows the error).
/// `-compare` takes two patterns that expand to the same number of signals and adds one compare row per pair
/// (in order); `error`, if given, receives why nothing was added.
std::size_t applyAddWave(DisplayModel& model, const WaveSpace& space, const AddWaveRequest& request,
                         std::string* error = nullptr);

/// Color name (red, green, blue, yellow, orange, white, cyan, magenta, gray, pink) or #rrggbb.
std::optional<std::uint32_t> parseColor(std::string_view text);

/// The wave window as a script of `add wave` commands (common syntax plus a few extensions: -vbus,
/// -collapse, -bits). Groups nested more than one level are flattened to names like "outer/inner".
/// Sourcing the script in the simulator or importDoScript() rebuilds the rows.
std::string exportDoScript(const DisplayModel& model);

struct ImportResult {
    std::size_t rowsAdded = 0;
    std::size_t skipped = 0;         ///< lines that are not `add wave` commands (comments excluded)
    std::vector<std::string> errors; ///< "line N: ..." for add wave commands that failed
};

/// Applies the `add wave` commands of a script to the model. Everything else is skipped and counted.
ImportResult importDoScript(std::string_view script, DisplayModel& model, const WaveStore& store);
ImportResult importDoScript(std::string_view script, DisplayModel& model, const WaveSpace& space);

/// A dotted path, optionally with a dataset prefix, as a command writes it: "ref:tb.x" -> "ref:/tb/x".
std::string commandPath(std::string_view prefixedDotted);

} // namespace lvd::wave
