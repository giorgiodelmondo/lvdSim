// SPDX-License-Identifier: GPL-3.0-or-later
#include <lvdwave/version.hpp>

namespace lvd::wave {

std::string_view version() noexcept {
    return LVD_VERSION_STRING; // from project(VERSION) in the top-level CMakeLists.txt
}

} // namespace lvd::wave
