// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace lvd::wave {

/// Library version string, "major.minor.patch". O(1).
std::string_view version() noexcept;

} // namespace lvd::wave
