// SPDX-License-Identifier: GPL-3.0-or-later
// The project commands of lvdsim-cli (docs/projects.md):
//   lvdsim-cli compile  <project.lvdproj> [--all] [--dry-run] [FILE...]
//   lvdsim-cli regress  <project.lvdproj> [-j N] [--junit FILE] [--test NAME]... [--rerun-failed]
//                       [--no-compile] [--timeout S] [--wave] [--coverage] [--out DIR]
//   lvdsim-cli project  import <script.do> [-o project.lvdproj] [--ini lvdsim.ini] [--name NAME]
//   lvdsim-cli project  status <project.lvdproj>
#pragma once

#include "environment.hpp"

#include <string>
#include <vector>

namespace lvd::tools {

int runCompileCommand(const std::vector<std::string>& args, const Environment& env);
int runRegressCommand(const std::vector<std::string>& args, const Environment& env);
int runProjectCommand(const std::vector<std::string>& args, const Environment& env);

} // namespace lvd::tools
