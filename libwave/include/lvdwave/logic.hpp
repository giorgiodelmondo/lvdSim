// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace lvd::wave {

/// Nine-valued logic: covers Verilog 0/1/x/z and VHDL std_logic (U X 0 1 Z W L H -).
/// Stored in 4 bits per bit. O(1) conversions.
enum class Logic : std::uint8_t { L0 = 0, L1, X, Z, U, W, L, H, DontCare };

inline constexpr unsigned kLogicCount = 9;

/// Character used in waveform files and in displayed binary strings.
char toChar(Logic v) noexcept;

/// Parse a waveform character, case-insensitive. Unknown characters map to X.
Logic fromChar(char c) noexcept;

/// True for values that carry a definite numeric 0 or 1 (0, 1, and weak L, H).
constexpr bool isKnown(Logic v) noexcept {
    return v == Logic::L0 || v == Logic::L1 || v == Logic::L || v == Logic::H;
}

/// Numeric value of a known bit (L counts as 0, H as 1). Undefined for unknown bits.
constexpr bool bitValue(Logic v) noexcept { return v == Logic::L1 || v == Logic::H; }

}  // namespace lvd::wave
