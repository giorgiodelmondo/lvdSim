// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>

class QMimeData;

namespace lvd::gui {

/// Drag and drop of signals from the Objects pane to the wave window. The payload is a list of full
/// hierarchical signal paths written the way `add wave` takes them ("/top/dut/clk": '/' separated, the design
/// name first). Besides the custom format the mime data carries the same paths as text/plain, one per line, so
/// they can be dropped into the transcript or an editor.
inline constexpr char kSignalPathsMime[] = "application/x-lvdsim-signal-paths";

/// "top.dut.clk" (the dotted path of the wave store) as the `add wave` path "/top/dut/clk".
QString addWavePathFromStorePath(const QString& dotted);

/// Mime data for the paths (the caller owns it, normally by handing it to a QDrag).
QMimeData* makeSignalMime(const QStringList& addWavePaths);
bool hasSignalPaths(const QMimeData* mime);
/// The paths, in the order they were put in; empty for foreign mime data.
QStringList signalPathsFromMime(const QMimeData* mime);

} // namespace lvd::gui
