// SPDX-License-Identifier: GPL-3.0-or-later
#include <lvdwave/radix.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <vector>

namespace lvd::wave {

const char* radixName(Radix r) noexcept {
    switch (r) {
        case Radix::Binary: return "binary";
        case Radix::Octal: return "octal";
        case Radix::Hex: return "hex";
        case Radix::Unsigned: return "unsigned";
        case Radix::Signed: return "signed";
        case Radix::Ascii: return "ascii";
        case Radix::Default: return "default";
    }
    return "default";
}

bool parseRadix(std::string_view name, Radix& out) noexcept {
    std::string n(name);
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return std::tolower(c); });
    struct Entry { const char* name; Radix r; };
    static constexpr Entry kEntries[] = {
        {"binary", Radix::Binary}, {"bin", Radix::Binary},     {"octal", Radix::Octal},
        {"oct", Radix::Octal},     {"hex", Radix::Hex},        {"hexadecimal", Radix::Hex},
        {"unsigned", Radix::Unsigned}, {"decimal", Radix::Unsigned}, {"signed", Radix::Signed},
        {"ascii", Radix::Ascii},   {"default", Radix::Default},
    };
    for (const Entry& e : kEntries) {
        if (n == e.name) {
            out = e.r;
            return true;
        }
    }
    return false;
}

namespace {

bool isOne(char c) { return c == '1' || c == 'H'; }
bool isZero(char c) { return c == '0' || c == 'L'; }
bool isDefinite(char c) { return isOne(c) || isZero(c); }

// Digit for a group of bits (MSB first) of up to 4 bits.
char groupDigit(std::string_view g) {
    bool allZ = true, allU = true, definite = true;
    for (char c : g) {
        allZ = allZ && c == 'Z';
        allU = allU && c == 'U';
        definite = definite && isDefinite(c);
    }
    if (!definite) return allZ ? 'Z' : (allU ? 'U' : 'X');
    unsigned v = 0;
    for (char c : g) v = (v << 1) | (isOne(c) ? 1u : 0u);
    return "0123456789ABCDEF"[v];
}

std::string groupedDigits(std::string_view bits, std::size_t groupSize) {
    std::string out;
    const std::size_t first = bits.size() % groupSize;  // size of the leading partial group
    std::size_t pos = 0;
    if (first != 0) {
        out += groupDigit(bits.substr(0, first));
        pos = first;
    }
    for (; pos < bits.size(); pos += groupSize) out += groupDigit(bits.substr(pos, groupSize));
    return out;
}

// Decimal string of an unsigned value given as bits MSB first (all definite). Schoolbook double-dabble.
std::string unsignedDecimal(std::string_view bits) {
    std::vector<std::uint8_t> digits{0};  // little-endian decimal digits
    for (char c : bits) {
        int carry = isOne(c) ? 1 : 0;
        for (auto& d : digits) {
            const int v = d * 2 + carry;
            d = static_cast<std::uint8_t>(v % 10);
            carry = v / 10;
        }
        if (carry != 0) digits.push_back(static_cast<std::uint8_t>(carry));
    }
    std::string out;
    out.reserve(digits.size());
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) out += static_cast<char>('0' + *it);
    return out;
}

// Two's complement negation of a definite bit string, returned as an unsigned magnitude bit string.
std::string magnitudeOfNegative(std::string_view bits) {
    std::string inv(bits.size(), '0');
    for (std::size_t i = 0; i < bits.size(); ++i) inv[i] = isOne(bits[i]) ? '0' : '1';
    for (std::size_t i = inv.size(); i-- > 0;) {  // add one
        if (inv[i] == '0') {
            inv[i] = '1';
            break;
        }
        inv[i] = '0';
    }
    return inv;
}

std::string dominantUnknown(std::string_view bits) {
    bool allZ = true, allU = true;
    for (char c : bits) {
        allZ = allZ && c == 'Z';
        allU = allU && c == 'U';
    }
    return std::string(1, allZ ? 'Z' : (allU ? 'U' : 'X'));
}

}  // namespace

std::string formatBits(std::string_view bits, Radix radix) {
    if (bits.empty()) return {};
    if (radix == Radix::Default) radix = bits.size() == 1 ? Radix::Binary : Radix::Hex;

    switch (radix) {
        case Radix::Binary:
            return std::string(bits);
        case Radix::Octal:
            return groupedDigits(bits, 3);
        case Radix::Hex:
            return groupedDigits(bits, 4);
        case Radix::Unsigned:
        case Radix::Signed: {
            if (!std::all_of(bits.begin(), bits.end(), isDefinite)) return dominantUnknown(bits);
            if (radix == Radix::Signed && isOne(bits.front()))
                return "-" + unsignedDecimal(magnitudeOfNegative(bits));
            return unsignedDecimal(bits);
        }
        case Radix::Ascii: {
            std::string out;
            const std::size_t first = bits.size() % 8;
            auto emit = [&](std::string_view g) {
                if (!std::all_of(g.begin(), g.end(), isDefinite)) {
                    out += '?';
                    return;
                }
                unsigned v = 0;
                for (char c : g) v = (v << 1) | (isOne(c) ? 1u : 0u);
                out += (v >= 32 && v < 127) ? static_cast<char>(v) : '.';
            };
            std::size_t pos = 0;
            if (first != 0) {
                emit(bits.substr(0, first));
                pos = first;
            }
            for (; pos < bits.size(); pos += 8) emit(bits.substr(pos, 8));
            return out;
        }
        case Radix::Default:
            break;
    }
    return std::string(bits);
}

std::string formatValue(const Signal& signal, std::size_t change, Radix radix) {
    switch (signal.kind()) {
        case SignalKind::Logic:
            return formatBits(signal.bits(change), radix);
        case SignalKind::Real: {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.15g", signal.real(change));
            return buf;
        }
        case SignalKind::String:
            return std::string(signal.text(change));
    }
    return {};
}

}  // namespace lvd::wave
