// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/declaration_locator.hpp"

#include <QString>

#include <functional>
#include <optional>
#include <project.hpp>

namespace lvd::gui {

/// Where the Library pane finds the source of a design unit when no simulation can be asked:
///  1. the open project: the file of the project library that declares the unit (found by the unit scanner);
///  2. nvc itself: `nvc --work=lib:dir --print-deps unit` names the file each VHDL unit was analysed from
///     (nvc does not answer it for Verilog modules).
/// The line is then found by scanning that file for the unit.

/// The unit ("counter", or its architecture "rtl" when not empty) in a project library (compared without
/// case); `absolute` turns a project file path into an absolute one. Reads the files of that library.
std::optional<SourceLocation> findUnitInProject(const lvd::tools::proj::Project& project,
                                                const std::function<QString(const QString&)>& absolute,
                                                const QString& library, const QString& unit,
                                                const QString& architecture);

/// The line (1 based) where a file declares the unit or architecture; 0 if the scanner does not find it.
int unitLineInFile(const QString& file, const QString& unit, const QString& architecture);

/// The source file of `library.unit` (or of its architecture) in the output of `nvc --print-deps`: the first
/// name after the colon of the line whose target is `<dir>/LIBRARY.UNIT` (`LIBRARY.UNIT-ARCH`). Empty if no
/// line names it. The name is as nvc stored it (often relative to the directory of the analysis).
QString sourceFromPrintDeps(const QString& output, const QString& library, const QString& unit,
                            const QString& architecture);

} // namespace lvd::gui
