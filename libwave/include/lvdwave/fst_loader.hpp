// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvdwave/result.hpp>
#include <lvdwave/store.hpp>

#include <string>

namespace lvd::wave {

/// Load a complete FST file into a WaveStore.
///
/// FST files can only be read once the writer has closed them, so this is not suitable for files
/// that are still being written. Live updates use LiveReader (live_reader.hpp) instead.
/// Complexity is linear in the number of value changes.
Result<WaveStore> loadFst(const std::string& path);

}  // namespace lvd::wave
