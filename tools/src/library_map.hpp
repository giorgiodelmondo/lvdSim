// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lvd::tools {

/// Library name to directory mappings from the [Library] section of a lvdsim.ini style file.
/// Names are case-insensitive. Other sections and keys are ignored when reading and preserved when
/// the file text is updated.
class LibraryMap {
public:
    static LibraryMap parse(std::string_view iniText);

    std::optional<std::string> pathOf(std::string_view name) const;
    void set(std::string_view name, std::string path);
    const std::vector<std::pair<std::string, std::string>>& entries() const noexcept { return entries_; }

private:
    std::vector<std::pair<std::string, std::string>> entries_;
};

/// Text of an ini file with `name = path` set in its [Library] section (replacing an existing entry,
/// or adding the entry or the section). Line endings of the input are kept.
std::string withLibraryMapping(std::string_view iniText, std::string_view name, std::string_view path);

/// Where a library lives: the mapping if there is one, otherwise a directory with the library's name in
/// the current directory. nullopt if neither exists.
std::optional<std::string> resolveLibrary(std::string_view name, const LibraryMap& map,
                                          const std::function<bool(const std::string&)>& dirExists);

}  // namespace lvd::tools
