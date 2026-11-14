// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sim/parsers/json_replies.hpp"

#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace lvd::gui {

/// True when both name the same file: compared as absolute, cleaned paths, ignoring case on Windows.
bool sameSourceFile(const QString& a, const QString& b);
/// The absolute, cleaned form of a file name (relative names are taken from the current directory).
QString absoluteSourcePath(const QString& file);

/// A source line breakpoint as the GUI keeps it. The kernel knows only the enabled ones: a disabled
/// breakpoint is deleted in the kernel and added again when it is enabled.
struct LineBreakpoint {
    enum class State {
        Planned,  ///< no simulation: it is sent when one starts
        Pending,  ///< sent, the reply has not arrived
        Verified, ///< the kernel took it (ids)
        Rejected, ///< the kernel refused it (note says why)
    };
    QString file; ///< absolute path
    int line = 0; ///< 1 based
    bool enabled = true;
    State state = State::Planned;
    QVector<int> ids; ///< kernel ids (more than one if the same line was also added in the shell)
    int hits = 0;     ///< stops counted by the kernel in this simulation
    QString note;     ///< reason of a rejection
};

/// The line breakpoints of one design: toggled in the Source gutter, listed in the Breakpoints dock, kept
/// in the settings and merged with what the kernel lists (breakpoints typed in the shell). Plain data
/// without Qt widgets so that it can be tested alone.
class LineBreakpointList {
public:
    const QVector<LineBreakpoint>& items() const noexcept { return items_; }
    int size() const noexcept { return static_cast<int>(items_.size()); }
    bool isEmpty() const noexcept { return items_.isEmpty(); }
    LineBreakpoint& operator[](int i) { return items_[i]; }
    const LineBreakpoint& operator[](int i) const { return items_[i]; }

    int indexOf(const QString& file, int line) const;
    int indexOfId(int id) const;
    /// Adds an enabled breakpoint (state Planned) and returns its index, or the index of the one already
    /// there.
    int add(const QString& file, int line, bool enabled = true);
    bool remove(const QString& file, int line);
    void clear() { items_.clear(); }
    /// The breakpoints of one file.
    QVector<LineBreakpoint> inFile(const QString& file) const;

    /// A new simulation: every breakpoint is Planned again, without ids and hits.
    void resetKernelState();

    /// Applies `breakpoint list -json`. Ids already known update their hit counts; unknown ids (added in the
    /// shell) join the breakpoint on the same line or become new entries, their file resolved by `resolve`.
    /// A breakpoint that was verified and whose ids are all gone was deleted in the shell and is removed.
    /// Pending, rejected, planned and disabled breakpoints are left alone.
    void mergeKernelList(const QVector<LineBreakpointInfo>& list,
                         const std::function<QString(const QString&)>& resolve);

    /// One string per breakpoint for the settings: "1|12|C:/x/tb.vhd" (enabled, line, file).
    QStringList serialize() const;
    static LineBreakpointList deserialize(const QStringList& entries);

private:
    QVector<LineBreakpoint> items_;
};

} // namespace lvd::gui
