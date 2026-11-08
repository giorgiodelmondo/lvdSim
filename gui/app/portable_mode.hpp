// SPDX-License-Identifier: GPL-3.0-or-later
// Portable mode: settings and compiled libraries live next to the program instead of the registry / user
// folders. Portable when a file `portable.txt` sits next to the executable, or LVDSIM_PORTABLE is
// 1/true/yes/on.
#pragma once

#include <QSettings>
#include <QString>

#include <functional>
#include <memory>

namespace lvd::gui {

struct PortableDecision {
    bool portable = false;  ///< use the files below
    bool requested = false; ///< marker or environment asked for it (false when only the fallback applies)
    QString appDir;
    QString dataDir;      ///< <appDir>/data
    QString settingsFile; ///< <appDir>/data/lvdsim-gui.ini (empty unless portable)
    QString librariesDir; ///< <appDir>/data/libraries (empty unless portable)
    QString note;         ///< set when portable was requested but the folder is not writable
};

/// Pure decision. `envValue` is the value of LVDSIM_PORTABLE (empty when unset). `isWritable` tells whether
/// the data directory can be used (default: create it and write a probe file); injected by the tests.
PortableDecision resolvePortable(const QString& appDir, const QString& envValue,
                                 const std::function<bool(const QString& dataDir)>& isWritable = {});

/// Folder that holds portable.txt and data/: the program directory, or its parent in the Linux bundle layout
/// (<root>/bin/lvdsim-gui next to <root>/AppRun).
QString portableBaseDir(const QString& programDir);

/// Decision for the running program (application directory, the real environment).
PortableDecision portableDecision();

/// The one place that opens the GUI settings: an explicit ini file wins, then the portable ini, then the
/// per-user store (registry on Windows).
std::unique_ptr<QSettings> openGuiSettings(const QString& explicitFile);

/// Default folder for compiled libraries: <app>/data/libraries when portable, else <user
/// data>/lvdsim/libraries.
QString defaultLibrariesDir();

} // namespace lvd::gui
