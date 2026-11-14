// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvdwave/result.hpp>

#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QVector>

#include <optional>

namespace lvd::gui {

/// Readers for the `-json` replies of the fork (schema version 1, docs/protocol.md). Every function takes the
/// reply line as the shell printed it and returns the error text instead of throwing when it is not valid
/// JSON, not an object, has an unknown "v", or lacks a member the command is documented to return. Members
/// that are not known are ignored, as the schema requires.
inline constexpr int kSchemaVersion = 1;

/// A time breakpoint of `breakpoint add/list -json`.
struct BreakpointInfo {
    int id = 0;
    qint64 timeFs = 0;
};

/// A source line breakpoint of `breakpoint add/list -json` (members "file" and "line" instead of "time").
/// Time and source line breakpoints are numbered in one sequence by the kernel.
struct LineBreakpointInfo {
    int id = 0;
    QString file; ///< as given to `breakpoint add`
    int line = 0; ///< 1 or more
    int hits = 0; ///< how often a process stopped there
};

/// Interrupted (change set 41): the run was ended by a break request (Ctrl-C, Ctrl-Break, the Break button)
/// before its next time step; the next `run` goes on from "now", as after a time breakpoint.
enum class RunStatus { Paused, Finished, Breakpoint, Interrupted };

/// Reply of `run` and `step`.
struct RunReply {
    qint64 nowFs = 0;
    qint64 delta = 0;
    RunStatus status = RunStatus::Paused;
    std::optional<BreakpointInfo> breakpoint; ///< set when status is Breakpoint
};

struct ProcessInfo {
    QString name;
    QString path;
    QString state; ///< ready, delayed or waiting
    QString file;  ///< empty when unknown
    int line = 0;  ///< 0 when unknown
};

/// How a signal reaches its region: the mode of a port, or Internal for a signal declared in the region.
/// Unknown means the simulator did not say (the member is optional, see docs/protocol.md).
enum class PortMode { Unknown, In, Out, InOut, Buffer, Internal };

/// "in", "out", "inout", "buffer" and "internal" (any case) to a mode; anything else, "linkage" included, is
/// Unknown.
PortMode portModeFromString(const QString& text);
/// Lower case VHDL word for a mode ("in", "out", "inout", "buffer", "internal"), empty for Unknown.
QString portModeName(PortMode mode);

/// One entry of the reply of `describe -json`.
struct DescribedObject {
    QString name;
    QString path;
    QString kind; ///< "signal" or "region"; with PR 10 also "generic", "constant", "variable"
    QString type; ///< empty for regions
    PortMode mode = PortMode::Unknown; ///< optional member "mode"

    /// Members of `describe -json -location` (kernel PR 10, docs/protocol.md). The kernel gives every value
    /// as a string; numbers are taken too. Empty or 0 when the kernel did not send them.
    QString file;   ///< declaration of an object (as given at analysis)
    int line = 0;   ///< 1 based
    int column = 0; ///< 1 based, 0 when unknown
    QString unit; ///< region: "lib.entity(arch)", "lib.module", or the label of a block or generate statement
    QString unitFile;
    int unitLine = 0;
    QString instFile; ///< region: the statement that instantiates it in the parent (not for the top level)
    int instLine = 0;

    /// True when the reply carried a declaration (objects) or a definition (regions).
    bool hasLocation() const noexcept {
        return (!file.isEmpty() && line > 0) || (!unitFile.isEmpty() && unitLine > 0);
    }
};

/// One frame of the call stack of a stopped process (`stack -json`, and the "frame" of a stop reply).
struct SourceFrame {
    int level = 0;  ///< 0 is the innermost frame
    QString kind;   ///< "process", "function", "procedure" or "other"
    QString name;   ///< the process path (as in `processes`) or the subprogram name
    QString symbol; ///< the kernel's unit name, unique per subprogram overload
    QString file;   ///< as the kernel knows it (as given at analysis); empty when unknown
    int line = 0;   ///< 0 when unknown
};

/// Reply of `run`, `step` (and `run` given while stopped) when a process stopped at a source line breakpoint
/// or after a statement step: "status" is "stopped". The run is still in progress in the kernel; the shell
/// shows its prompt and reads commands (`stack`, `locals`, `examine`...) until `run` or `step` resumes it.
/// The reply of the resuming command is the next run reply (finished, paused, breakpoint or stopped again).
/// Why a process stopped at a source line ("reason" of the stop reply).
enum class StopReason {
    Breakpoint, ///< "breakpoint": a source line breakpoint (with its id)
    Step,       ///< "step": a statement step ended
    Stop,       ///< "stop" (change set 36): the process called Verilog $stop; `run` continues after it
};

struct StopReply {
    qint64 nowFs = 0;
    qint64 delta = 0;
    StopReason reason = StopReason::Step;
    bool atBreakpoint = false; ///< reason == StopReason::Breakpoint (kept for older callers)
    int breakpointId = 0;      ///< set when atBreakpoint
    QString process;           ///< path of the stopped process
    SourceFrame frame;         ///< innermost frame: where it stopped (for reason Stop it may be missing:
                               ///< then name and file are empty and line is 0)
};

/// Reply of `restart -json` (status "restarted"). The kernel has a new model at time zero; the shell, Tcl
/// state, source line breakpoints, the time breakpoints not reached yet and the forces of the `force` command
/// (unless
/// `-nobreakpoint` / `-noforce`) are kept. A restart given while a process is stopped at a source line
/// finishes the current cycle first: the reply then comes as the reply of the `run` or `step` that was
/// interrupted.
struct RestartReply {
    qint64 nowFs = 0;    ///< always 0
    qint64 delta = 0;    ///< always 0
    int breakpoints = 0; ///< time and source line breakpoints kept
    int forces = 0;      ///< forces applied again (contiguous ranges)
};

/// One entry of `locals -json`.
struct VariableInfo {
    QString name;          ///< lower case
    QString kind;          ///< "variable", "constant" or "parameter"
    QString type;          ///< VHDL type name as the kernel prints it
    QString value;         ///< VHDL literal syntax ('1', "0101", 42, 1.5, (a => 7, b => '1'))
    bool hasValue = false; ///< false when the kernel cannot show the type (value null)
};

struct LocalsReply {
    int frame = 0;
    QVector<VariableInfo> variables;
};

/// Parses the line and checks "v". The first step of every reader below.
lvd::Result<QJsonObject> parseReply(const QString& text);

lvd::Result<RunReply> parseRunReply(const QString& text);
lvd::Result<QVector<ProcessInfo>> parseProcessesReply(const QString& text);
/// The time breakpoints of `breakpoint list -json`; source line breakpoints in the list are skipped.
lvd::Result<QVector<BreakpointInfo>> parseBreakpointList(const QString& text);
/// Reply of `breakpoint add -json <time> <unit>`.
lvd::Result<BreakpointInfo> parseBreakpointAdded(const QString& text);
/// The source line breakpoints of `breakpoint list -json`; time breakpoints in the list are skipped.
lvd::Result<QVector<LineBreakpointInfo>> parseLineBreakpointList(const QString& text);
/// Reply of `breakpoint add -json <file>:<line>` ("hits" is not part of it and stays 0).
lvd::Result<LineBreakpointInfo> parseLineBreakpointAdded(const QString& text);
/// Reply of `describe -json`. "mode" is optional: kernels that do not report it give PortMode::Unknown. The
/// location members are optional too (old kernels, or no -location option): then hasLocation() is false.
lvd::Result<QVector<DescribedObject>> parseDescribeReply(const QString& text);
lvd::Result<int> parseDeleted(const QString& text);

/// True when the line is a valid reply whose "status" is "stopped". Check it before parseRunReply, which
/// rejects that status (RunStatus has no value for it so that existing switches stay complete).
bool isStopReply(const QString& text);
lvd::Result<StopReply> parseStopReply(const QString& text);
/// Reply of `stack -json`, innermost frame first.
lvd::Result<QVector<SourceFrame>> parseStackReply(const QString& text);
/// Reply of `locals -json`.
lvd::Result<LocalsReply> parseLocalsReply(const QString& text);

/// True when the line is a valid reply whose "status" is "restarted". A `run -json` or `step -json` that was
/// interrupted by a restart at a source line gives this reply, so check it before parseRunReply, which
/// rejects that status.
bool isRestartReply(const QString& text);
/// Reply of `restart -json`, or of the interrupted `run`/`step` (see RestartReply).
lvd::Result<RestartReply> parseRestartReply(const QString& text);

/// "2.5" and "us" as femtoseconds, computed exactly from the digits. Empty if the number is not a plain
/// decimal, the unit is not fs, ps, ns, us or ms, or the value needs more than whole femtoseconds. The kernel
/// takes whole numbers only, so the GUI converts decimals before it sends a time.
std::optional<qint64> timeToFemtoseconds(const QString& number, const QString& unit);

/// Simulation time in the largest unit that shows it exactly, "50 ns", "1500 ps", "0 fs".
QString formatFemtoseconds(qint64 fs);

} // namespace lvd::gui

Q_DECLARE_METATYPE(lvd::gui::RunReply)
