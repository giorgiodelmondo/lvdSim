// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvdwave/store.hpp>

#include <string>
#include <string_view>

namespace lvd::wave {

enum class Radix : std::uint8_t {
    Binary,
    Octal,
    Hex,
    Unsigned,
    Signed,
    Ascii,
    Default,  ///< Binary for single bits, hexadecimal for vectors.
};

const char* radixName(Radix r) noexcept;
/// Parses names like "hex", "binary", "unsigned", "signed", "ascii", "default" (case-insensitive).
/// Returns false for unknown names.
bool parseRadix(std::string_view name, Radix& out) noexcept;

/// Format a bit string (most significant bit first, characters as in toChar) in a radix.
///
/// Digit groups containing any unknown bit print X (or Z if every bit of the group is Z, or U if every
/// bit is U). Decimal output is X, Z or U when any bit is not 0/1/L/H, otherwise exact for any width.
/// Weak L and H count as 0 and 1. Linear in the string length (decimal is quadratic in width).
std::string formatBits(std::string_view bitsMsbFirst, Radix radix);

/// Format one change of a signal. Real values print with up to 15 significant digits, String values
/// verbatim. The radix applies to Logic signals only.
std::string formatValue(const Signal& signal, std::size_t change, Radix radix);

}  // namespace lvd::wave
