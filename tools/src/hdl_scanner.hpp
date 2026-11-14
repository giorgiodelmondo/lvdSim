// SPDX-License-Identifier: GPL-3.0-or-later
// A light scanner for the design units a VHDL or Verilog file declares and the units it refers to. It is not
// a parser: comments, strings and character literals are skipped and a few token patterns are recognised.
// That is enough to order files for analysis and to know which files must be analysed again when one changes.
// Names are returned in lower case.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace lvd::tools::scan {

enum class UnitKind {
    Entity,
    Architecture,
    Package,
    PackageBody,
    Configuration,
    Context,
    Module,        ///< Verilog module, macromodule, interface, program or primitive
    VerilogPackage ///< SystemVerilog package
};

/// True for units other units can name (everything but architectures and package bodies).
bool isPrimary(UnitKind k);

struct UnitDecl {
    UnitKind kind = UnitKind::Entity;
    std::string name;    ///< architecture name for an architecture, package name for a body
    std::string primary; ///< the entity of an architecture or configuration, the package of a body
    int line = 0;
};

/// A reference to another unit. `library` is empty when the text names the unit without a library (a
/// component or module instance, a SystemVerilog package import): it is then looked up in the file's own
/// library first and then in the others.
struct UnitRef {
    std::string library;
    std::string name;
    int line = 0;
};

struct ScanResult {
    std::vector<UnitDecl> units;
    std::vector<UnitRef> refs;
    std::vector<std::string> includes; ///< Verilog `include file names as written
};

ScanResult scanVhdl(std::string_view text);
ScanResult scanVerilog(std::string_view text);

} // namespace lvd::tools::scan
