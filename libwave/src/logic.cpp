// SPDX-License-Identifier: GPL-3.0-or-later
#include <lvdwave/logic.hpp>

namespace lvd::wave {

char toChar(Logic v) noexcept {
    static constexpr char kChars[kLogicCount] = {'0', '1', 'X', 'Z', 'U', 'W', 'L', 'H', '-'};
    return kChars[static_cast<unsigned>(v)];
}

Logic fromChar(char c) noexcept {
    switch (c) {
        case '0': return Logic::L0;
        case '1': return Logic::L1;
        case 'x': case 'X': return Logic::X;
        case 'z': case 'Z': return Logic::Z;
        case 'u': case 'U': return Logic::U;
        case 'w': case 'W': return Logic::W;
        case 'l': case 'L': return Logic::L;
        case 'h': case 'H': return Logic::H;
        case '-': return Logic::DontCare;
        default: return Logic::X;
    }
}

}  // namespace lvd::wave
