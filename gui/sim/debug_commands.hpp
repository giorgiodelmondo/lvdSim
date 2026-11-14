// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace lvd::gui {

/// Command lines for the source debugger of the kernel's shell (docs/protocol.md, "Source line breakpoints").
/// Every command asks for the JSON reply; send them with NvcSession::send and read the reply with the
/// matching reader of sim/parsers/json_replies.hpp.

/// A Tcl word that stands for `text` as it is: unchanged when it has no special characters, in braces
/// when that is safe, otherwise with each special character escaped.
inline QString tclWord(const QString& text) {
    static const QString kSpecial = QStringLiteral(" \t\r\n;\"$[]{}\\");
    bool special = text.isEmpty();
    for (QChar c : text)
        special = special || kSpecial.contains(c);
    if (!special)
        return text;
    if (!text.contains(QLatin1Char('{')) && !text.contains(QLatin1Char('}')) && !text.endsWith(QLatin1Char('\\')))
        return QLatin1Char('{') + text + QLatin1Char('}');
    QString out;
    for (QChar c : text) {
        if (c == QLatin1Char('\n'))
            out += QLatin1String("\\n");
        else if (c == QLatin1Char('\r'))
            out += QLatin1String("\\r");
        else if (c == QLatin1Char('\t'))
            out += QLatin1String("\\t");
        else {
            if (kSpecial.contains(c))
                out += QLatin1Char('\\');
            out += c;
        }
    }
    return out;
}

/// `breakpoint add -json <file>:<line>`; reply: parseLineBreakpointAdded. The file matches the path the kernel
/// knows (as given at analysis) or its last components, so a base name is enough when it is unique.
inline QString lineBreakpointCommand(const QString& file, int line) {
    return QStringLiteral("breakpoint add -json ") + tclWord(file + QLatin1Char(':') + QString::number(line));
}

enum class StepKind { Into, Over, Out };

/// `step -into|-over|-out -json`; reply: isStopReply/parseStopReply, or parseRunReply when the run ended first.
/// Into also works when nothing is stopped (runs until any process starts a statement).
inline QString statementStepCommand(StepKind kind) {
    switch (kind) {
    case StepKind::Into:
        return QStringLiteral("step -into -json");
    case StepKind::Over:
        return QStringLiteral("step -over -json");
    case StepKind::Out:
        return QStringLiteral("step -out -json");
    }
    return {};
}

/// `run -json`: while stopped it resumes the stopped run (no time limit is allowed then).
inline QString resumeCommand() {
    return QStringLiteral("run -json");
}

/// `stack -json`; reply: parseStackReply.
inline QString stackCommand() {
    return QStringLiteral("stack -json");
}

/// `locals -frame N -json`; reply: parseLocalsReply.
inline QString localsCommand(int frame) {
    return QStringLiteral("locals -frame %1 -json").arg(frame);
}

/// `restart [-noforce] [-nobreakpoint] -json`: the kernel starts the same elaborated design again at time zero in
/// the running process. Reply: parseRestartReply; the live waveform stream carries an R record (the reader clears
/// the store, PollResult::restarted). While a process is stopped at a source line the reply comes as the reply of
/// the interrupted run or step (isRestartReply). After recompiling sources the process must be started again: the
/// kernel does not load a new elaboration.
inline QString restartCommand(bool keepForces = true, bool keepBreakpoints = true) {
    QString cmd = QStringLiteral("restart");
    if (!keepForces)
        cmd += QStringLiteral(" -noforce");
    if (!keepBreakpoints)
        cmd += QStringLiteral(" -nobreakpoint");
    return cmd + QStringLiteral(" -json");
}

} // namespace lvd::gui
