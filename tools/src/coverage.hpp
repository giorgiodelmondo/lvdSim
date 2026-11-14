// SPDX-License-Identifier: GPL-3.0-or-later
// Code coverage collected by nvc: the database is written by the simulator (`.ncdb`), turned into XML by
// `nvc --cover-export --format=xml` and read here. Nothing in this file runs a program: the functions at the
// end only plan the nvc calls (export, merge, HTML report, the elaboration options), the caller runs them.
//
// The XML is a tree of scopes (library, instances, signals, processes, statements, branches) whose leaves are
// bins with a hierarchical name and a hit count. A scope carries `file` and `line` only where they differ
// from its parent, so both are inherited while reading.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace lvd::tools::cov {

/// The kinds of coverage nvc collects, in the order the GUI shows them.
enum class Kind { Statement, Branch, Expression, Toggle, State, Functional };
inline constexpr int kKindCount = 6;
inline constexpr std::array<Kind, kKindCount> kAllKinds = {Kind::Statement, Kind::Branch, Kind::Expression,
                                                           Kind::Toggle,    Kind::State,  Kind::Functional};

/// Short column name: "Stmt", "Branch", "Expr", "Toggle", "FSM", "Func".
const char* kindLabel(Kind k);
/// The XML element of the kind ("statement", "branch", ...; "state" for FSM states).
const char* kindElement(Kind k);
/// The word of nvc's --cover option ("statement", ..., "fsm-state", "functional").
const char* kindOption(Kind k);

/// One counter of the database.
struct Bin {
    Kind kind = Kind::Statement;
    std::string hier; ///< full name, for example WORK.TOP.U1._P0._S0._B0.BIN_TRUE
    std::string
        label; ///< the bin part of the name: BIN_TRUE, BIN_0_TO_1, BIN_STATE.IDLE; empty for statements
    std::int64_t hits = 0;
    bool unreachable =
        false;      ///< toggle of a signal that can never change (nvc stores a flag instead of a count)
    int file = -1;  ///< index into Database::files, -1 if unknown
    int line = 0;   ///< 1 based, 0 if unknown
    int scope = -1; ///< the scope that lists the bin
    // Only the JSON export carries these; the XML export leaves the defaults.
    int column = 0;             ///< 1 based, 0 if unknown
    std::int64_t threshold = 1; ///< hits needed to count as covered (nvc's --cover threshold)
    /// Excluded by nvc: the exclude flag of the database, or (with `--cover=...,exclude-unreachable`) a
    /// toggle or expression term that cannot happen. nvc's report counts such a bin as covered; so do the
    /// totals here.
    bool excluded = false;
    bool userExcluded = false; ///< excluded by an exclude file or pragma of the user
    bool hit() const noexcept { return !unreachable && hits > 0 && hits >= threshold; }
    /// Counts as covered in the totals and line marks: hit, or excluded (as nvc's report counts it).
    bool covered() const noexcept { return hit() || excluded; }
};

struct Scope {
    std::string name;      ///< as nvc writes it (upper case unless an extended identifier)
    std::string blockName; ///< ENTITY-ARCHITECTURE for an instance, empty for other scopes
    std::string path;      ///< dotted name from the library down, for example WORK.TOP.U1
    int parent = -1;
    int instance = -1; ///< the innermost instance this scope belongs to (itself for an instance)
    int file = -1;
    int line = 0;
    std::vector<int> children; ///< scope indices
    std::vector<int> bins;     ///< bin indices listed directly in this scope
    bool isInstance() const noexcept { return !blockName.empty(); }
};

/// Totals and hits per kind.
struct Counts {
    std::array<std::int64_t, kKindCount> total{};
    std::array<std::int64_t, kKindCount> hit{};

    void add(const Bin& b);
    void add(const Counts& o);
    bool has(Kind k) const noexcept { return total[static_cast<int>(k)] > 0; }
    /// Percentage 0..100 of the kind; -1 when it has no bins.
    double percent(Kind k) const noexcept;
    std::int64_t allTotal() const noexcept;
    std::int64_t allHit() const noexcept;
    /// All kinds together (hit bins over all bins), -1 when empty. nvc's report calls this "average".
    double overall() const noexcept;
};

struct Database {
    std::string library;            ///< name of the outermost scope (the work library)
    std::vector<std::string> files; ///< source files as nvc wrote them (absolute, or relative to --relative)
    std::vector<Scope> scopes;      ///< scopes[0] is the library scope
    std::vector<Bin> bins;
    std::uint32_t mask = 0; ///< nvc's coverage mask (JSON export only; 0 when read from XML)

    bool empty() const noexcept { return bins.empty(); }
    /// Instances directly below the library (the top levels).
    std::vector<int> topInstances() const;
    /// Instances directly below an instance (searching through non-instance scopes).
    std::vector<int> childInstances(int instance) const;
    /// Bins of the instance itself, without those of the instances below it.
    std::vector<int> instanceBins(int instance) const;
    /// Counts of the instance itself, or of it and everything below it.
    Counts counts(int instance, bool recursive) const;
    /// Counts of the whole database.
    Counts totals() const;
    /// Scope by dotted path (case insensitive, the library prefix may be left out) or /slash/path; -1 if
    /// none.
    int findScope(std::string_view path) const;
    /// The file most bins of the instance are in, -1 if it has none.
    int mainFile(int instance) const;
};

/// What a source line of one instance looks like, combining every bin on it.
struct LineCoverage {
    std::int64_t hits = 0; ///< the largest statement count on the line (else the largest count of any bin)
    int bins = 0;          ///< bins on the line
    int hitBins = 0;       ///< of which hit
    std::array<int, kKindCount> missed{}; ///< bins not hit, by kind
    bool hasStatement = false;
    bool covered() const noexcept { return hitBins == bins; }
    bool uncovered() const noexcept { return hitBins == 0; }
};

/// Per line coverage of a file in one instance (with recursive, also of the instances below it that use the
/// file).
std::map<int, LineCoverage> lineCoverage(const Database& db, int instance, int file, bool recursive = false);

/// Toggle coverage of one signal (or port) of an instance, per bit.
struct ToggleSignal {
    std::string name; ///< the signal scope's name
    int scope = -1;
    int line = 0;
    int file = -1;
    struct Bit {
        std::string index;     ///< "(3)", ".FIELD(1)", empty for a scalar
        std::int64_t rise = 0; ///< 0 -> 1
        std::int64_t fall = 0; ///< 1 -> 0
        bool hasRise = false, hasFall = false;
        bool unreachable = false;
        bool excluded = false; ///< every bin of the bit is excluded (Bin::excluded)
        bool riseExcluded = false, fallExcluded = false;
        bool covered() const noexcept {
            return excluded || (!unreachable && (!hasRise || rise > 0 || riseExcluded) &&
                                (!hasFall || fall > 0 || fallExcluded));
        }
    };
    std::vector<Bit> bits;
    int coveredBits() const;
};
std::vector<ToggleSignal> toggleSignals(const Database& db, int instance);

/// FSM state coverage of one state variable of an instance.
struct StateVariable {
    std::string name;
    int scope = -1;
    int line = 0;
    int file = -1;
    std::vector<std::pair<std::string, std::int64_t>> states; ///< state name, times entered
};
std::vector<StateVariable> stateVariables(const Database& db, int instance);

/// Reads the output of `nvc --cover-export --format=xml`. Unknown elements are skipped; a document that is
/// not such an export (or is cut short) gives an error.
struct ParseResult {
    Database db;
    std::string error; ///< empty on success
    bool ok() const noexcept { return error.empty(); }
};
ParseResult parseExportXml(std::string_view xml);
/// Reads the output of `nvc --cover-export --format=json` (schema version 1, docs/protocol.md "Coverage
/// export"). Gives the same database as the XML export of the same file, plus Bin::column, threshold, the
/// exclusion flags and Database::mask. Unknown members are ignored; an unknown "v" is an error.
ParseResult parseExportJson(std::string_view json);
/// Either export: JSON when the text starts with '{', else XML.
ParseResult parseExport(std::string_view text);

/// Display form of an nvc name: VHDL names in lower case, extended identifiers (\...\) as they are.
std::string displayName(std::string_view name);
/// /top/u1 for the instance (library left out), "/" for the library scope.
std::string displayPath(const Database& db, int scope);

// ---- planning nvc calls
// ------------------------------------------------------------------------------------------

/// Options of the elaboration that enable coverage: --cover=<kinds> and --cover-file=<file>. An empty kinds
/// list means every kind.
std::vector<std::string> elaborationOptions(const std::vector<Kind>& kinds, const std::string& databaseFile);
/// Puts elaborationOptions() right after the `-e` of an nvc argument list (and removes coverage options that
/// were there). Returns false if there is no -e.
bool addElaborationOptions(std::vector<std::string>& nvcArgs, const std::vector<Kind>& kinds,
                           const std::string& databaseFile);
/// Parses "statement,branch,toggle" (and the letters s b e t f u, as in "sbt"); unknown words are ignored.
std::vector<Kind> parseKinds(std::string_view text);

enum class ExportFormat { Xml, Json };
/// nvc arguments (after the global ones) that write the merged export of the databases to `file`. Json needs
/// a kernel with change set 14 (`kernel/PATCHES.md`); an older nvc fails with "unknown format": use Xml then.
std::vector<std::string> exportArgs(const std::vector<std::string>& databases, const std::string& file,
                                    ExportFormat format = ExportFormat::Xml);
/// ... that merge the databases into one (`--cover-merge`, union of hierarchies).
std::vector<std::string> mergeArgs(const std::vector<std::string>& databases, const std::string& outFile);
/// ... that write nvc's HTML report into `outDir` (its start page is outDir/index.html).
std::vector<std::string> reportArgs(const std::vector<std::string>& databases, const std::string& outDir);

} // namespace lvd::tools::cov
