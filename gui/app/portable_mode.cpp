// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/portable_mode.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace lvd::gui {

namespace {

bool defaultWritable(const QString& dataDir) {
    if (!QDir().mkpath(dataDir))
        return false;
    QFile probe(dataDir + "/.write-test");
    if (!probe.open(QIODevice::WriteOnly))
        return false;
    probe.close();
    probe.remove();
    return true;
}

bool truthy(const QString& v) {
    const QString s = v.trimmed().toLower();
    return s == "1" || s == "true" || s == "yes" || s == "on";
}

} // namespace

PortableDecision resolvePortable(const QString& appDir, const QString& envValue,
                                 const std::function<bool(const QString&)>& isWritable) {
    PortableDecision d;
    d.appDir = QDir::cleanPath(appDir);
    d.dataDir = d.appDir + "/data";
    d.requested = truthy(envValue) || QFileInfo::exists(d.appDir + "/portable.txt");
    if (!d.requested)
        return d;
    const bool ok = isWritable ? isWritable(d.dataDir) : defaultWritable(d.dataDir);
    if (!ok) {
        d.note = QString("Portable mode requested, but %1 is not writable: settings are kept in the normal "
                         "per-user location instead.")
                     .arg(QDir::toNativeSeparators(d.appDir));
        return d;
    }
    d.portable = true;
    d.settingsFile = d.dataDir + "/lvdsim-gui.ini";
    d.librariesDir = d.dataDir + "/libraries";
    return d;
}

QString portableBaseDir(const QString& programDir) {
    const QDir d(programDir);
    if (d.dirName() == "bin" && QFileInfo::exists(d.absoluteFilePath("../AppRun")))
        return QDir::cleanPath(d.absoluteFilePath(".."));
    return QDir::cleanPath(d.absolutePath());
}

PortableDecision portableDecision() {
    return resolvePortable(portableBaseDir(QCoreApplication::applicationDirPath()),
                           qEnvironmentVariable("LVDSIM_PORTABLE"));
}

std::unique_ptr<QSettings> openGuiSettings(const QString& explicitFile) {
    if (!explicitFile.isEmpty())
        return std::make_unique<QSettings>(explicitFile, QSettings::IniFormat);
    const PortableDecision d = portableDecision();
    if (d.portable)
        return std::make_unique<QSettings>(d.settingsFile, QSettings::IniFormat);
    return std::make_unique<QSettings>("lvdsim", "lvdsim");
}

QString defaultLibrariesDir() {
    const PortableDecision d = portableDecision();
    if (d.portable)
        return d.librariesDir;
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/lvdsim/libraries";
}

} // namespace lvd::gui
