// SPDX-License-Identifier: GPL-3.0-or-later
// A design analysed with the real nvc into a temporary work library, for GUI tests that drive a simulation.
#pragma once

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include <environment.hpp>
#include <fstream>
#include <process.hpp>

namespace lvd::gui::testing {

inline QString envOr(const char* name, const QString& fallback) {
    const QByteArray v = qgetenv(name);
    return v.isEmpty() ? fallback : QString::fromLocal8Bit(v);
}

struct Nvc {
    QString exe, libDir, dllDir;
    bool found = false;
};

/// nvc of the kernel build tree, or LVDSIM_NVC / LVDSIM_NVC_LIBDIR / LVDSIM_NVC_DLLDIR.
inline Nvc findNvc() {
    Nvc n;
    const QString build = QStringLiteral(LVD_KERNEL_BUILD);
#ifdef Q_OS_WIN
    n.exe = envOr("LVDSIM_NVC", build + "/bin/nvc.exe");
    n.dllDir =
        envOr("LVDSIM_NVC_DLLDIR", QDir("C:/msys64/mingw64/bin").exists() ? "C:/msys64/mingw64/bin" : "");
#else
    n.exe = envOr("LVDSIM_NVC", build + "/bin/nvc");
    n.dllDir = envOr("LVDSIM_NVC_DLLDIR", "");
#endif
    n.libDir = envOr("LVDSIM_NVC_LIBDIR", build + "/lib");
    n.found = QFileInfo::exists(n.exe);
    return n;
}

/// The current directory is a temporary one holding `t.vhd` (or the file named) analysed into `work` while
/// the object lives. A name ending in `.v` gives a Verilog design.
struct AnalysedDesign {
    QTemporaryDir dir;
    Nvc nvc = findNvc();
    QString previousCwd = QDir::currentPath();

    bool prepare(const char* source, const char* fileName = "t.vhd") {
        if (!nvc.found || !dir.isValid())
            return false;
        QDir::setCurrent(dir.path());
        std::ofstream(fileName, std::ios::binary) << source;
        QDir().mkpath("work");
        std::ofstream("work/_NVC_LIB") << "lvdsim\n";
        lvd::tools::ProcessOptions po;
        po.capture = true;
        if (!nvc.dllDir.isEmpty())
            po.pathPrepend.push_back(nvc.dllDir.toStdString());
        const std::vector<std::string> args = {nvc.exe.toStdString(),
                                               "-L",
                                               nvc.libDir.toStdString(),
                                               "--std=08",
                                               "--work=work:work",
                                               "-a",
                                               fileName};
        const auto r = lvd::tools::runProcess(args, po);
        return r.started && r.exitCode == 0;
    }
    ~AnalysedDesign() { QDir::setCurrent(previousCwd); }

    QString file(const QString& name) const { return QDir(dir.path()).absoluteFilePath(name); }

    lvd::tools::Environment environment() const {
        lvd::tools::Environment e = lvd::tools::discoverEnvironment();
        e.nvc = nvc.exe.toStdString();
        e.nvcDllDir = nvc.dllDir.toStdString();
        e.ctx.nvcGlobalArgs = {"-L", nvc.libDir.toStdString()};
        return e;
    }
};

} // namespace lvd::gui::testing
