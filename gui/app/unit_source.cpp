// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/unit_source.hpp"

#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <hdl_scanner.hpp>

namespace lvd::gui {

namespace {

using lvd::tools::scan::ScanResult;
using lvd::tools::scan::UnitKind;

bool isVerilogFile(const QString& file) {
    return lvd::tools::proj::languageOf(file.toStdString()) != lvd::tools::proj::Language::Vhdl;
}

std::optional<ScanResult> scanFile(const QString& file, bool verilog) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    const std::string text = f.readAll().toStdString();
    return verilog ? lvd::tools::scan::scanVerilog(text) : lvd::tools::scan::scanVhdl(text);
}

/// The declaration of the unit (or architecture) in a scan, 0 if none.
int lineIn(const ScanResult& scan, const std::string& unit, const std::string& arch) {
    for (const auto& u : scan.units) {
        if (arch.empty()) {
            if (u.kind != UnitKind::Architecture && u.kind != UnitKind::PackageBody && u.name == unit)
                return u.line;
        } else if (u.kind == UnitKind::Architecture && u.name == arch && u.primary == unit) {
            return u.line;
        }
    }
    return 0;
}

} // namespace

int unitLineInFile(const QString& file, const QString& unit, const QString& architecture) {
    const auto scan = scanFile(file, isVerilogFile(file));
    if (!scan)
        return 0;
    return lineIn(*scan, unit.toLower().toStdString(), architecture.toLower().toStdString());
}

std::optional<SourceLocation> findUnitInProject(const lvd::tools::proj::Project& project,
                                                const std::function<QString(const QString&)>& absolute,
                                                const QString& library, const QString& unit,
                                                const QString& architecture) {
    const std::string u = unit.toLower().toStdString();
    const std::string a = architecture.toLower().toStdString();
    for (const auto& lib : project.libraries) {
        if (QString::fromStdString(lib.name).compare(library, Qt::CaseInsensitive) != 0)
            continue;
        for (const auto& f : lib.files) {
            const QString file = absolute(QString::fromStdString(f.path));
            const auto scan = scanFile(file, f.isVerilog());
            if (!scan)
                continue;
            if (const int line = lineIn(*scan, u, a); line > 0)
                return SourceLocation{file, line, 0, architecture.isEmpty() ? unit : architecture};
        }
    }
    return std::nullopt;
}

QString sourceFromPrintDeps(const QString& output, const QString& library, const QString& unit,
                            const QString& architecture) {
    QString target = (library + QLatin1Char('.') + unit).toUpper();
    if (!architecture.isEmpty())
        target += QLatin1Char('-') + architecture.toUpper();
    for (QString line : output.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        // "<dir>/WORK.UNIT: file.vhd dep dep" (the directory may contain a drive colon on Windows)
        const qsizetype colon = line.indexOf(QLatin1String(": "));
        if (colon <= 0)
            continue;
        const QString name = QFileInfo(line.left(colon)).fileName();
        if (name.compare(target, Qt::CaseInsensitive) != 0)
            continue;
        const QStringList deps = line.mid(colon + 2).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        return deps.value(0);
    }
    return {};
}

} // namespace lvd::gui
