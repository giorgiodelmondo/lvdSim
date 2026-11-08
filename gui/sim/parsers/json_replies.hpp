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
/// reply line as the shell printed it and returns the error text instead of throwing when it is not valid JSON,
/// not an object, has an unknown "v", or lacks a member the command is documented to return. Members that are
/// not known are ignored, as the schema requires.
inline constexpr int kSchemaVersion = 1;

struct BreakpointInfo {
    int id = 0;
    qint64 timeFs = 0;
};

enum class RunStatus { Paused, Finished, Breakpoint };

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

/// Parses the line and checks "v". The first step of every reader below.
lvd::Result<QJsonObject> parseReply(const QString& text);

lvd::Result<RunReply> parseRunReply(const QString& text);
lvd::Result<QVector<ProcessInfo>> parseProcessesReply(const QString& text);
lvd::Result<QVector<BreakpointInfo>> parseBreakpointList(const QString& text);
lvd::Result<BreakpointInfo> parseBreakpointAdded(const QString& text);
lvd::Result<int> parseDeleted(const QString& text);

/// "2.5" and "us" as femtoseconds, computed exactly from the digits. Empty if the number is not a plain
/// decimal, the unit is not fs, ps, ns, us or ms, or the value needs more than whole femtoseconds. The kernel
/// takes whole numbers only, so the GUI converts decimals before it sends a time.
std::optional<qint64> timeToFemtoseconds(const QString& number, const QString& unit);

/// Simulation time in the largest unit that shows it exactly, "50 ns", "1500 ps", "0 fs".
QString formatFemtoseconds(qint64 fs);

} // namespace lvd::gui

Q_DECLARE_METATYPE(lvd::gui::RunReply)
