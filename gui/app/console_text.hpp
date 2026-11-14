// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace lvd::gui {

/// Text rules of the console, without widgets (unit-tested in test_console_text).

/// True when `script` is a whole Tcl command (or several): braces, brackets and quotes are closed and the
/// last line does not end in a backslash. The same question `info complete` answers, decided in the GUI so
/// that an unfinished command is never sent to the shell (which reads one line per command).
bool tclCommandComplete(const QString& script);

/// Splits typed or pasted text into the commands to send: each complete group of lines is one command, so
/// pasted lines become separate commands and a multi-line `proc` stays one. Blank groups are dropped. An
/// unfinished rest is returned as the last element.
QStringList splitTopLevelCommands(const QString& script);

/// The shell reads one line per command. A command that spans lines is sent as `eval "..."` with the newlines
/// and the characters that are special inside double quotes escaped, which Tcl turns back into the original
/// script. A one-line command is returned unchanged.
QString oneLineCommand(const QString& command);

/// The text with the shell prompts ("% " at the start of a line, repeated) removed from every line.
QString stripPrompts(const QString& text);

/// How a transcript line is coloured: by the "** Error", "** Warning", "** Note" prefix (after any prompts).
enum class LineSeverity { None, Note, Warning, Error };
LineSeverity lineSeverity(const QString& line);

/// A reference to a source line inside a transcript line: `t.vhd:17`, `D:/src/t.vhd:17:5`,
/// `(file "run.do" line 3)` (Tcl error traces).
struct SourceRef {
    int start = 0;  ///< character offset of the link in the line
    int length = 0; ///< characters underlined
    QString file;
    int line = 0;
};
QVector<SourceRef> findSourceRefs(const QString& line);

/// The Tcl word for a file name in a command (`do {C:/x y/run.do}`): forward slashes, braced when needed.
QString tclFileWord(const QString& path);

} // namespace lvd::gui
