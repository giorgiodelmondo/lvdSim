// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/line_breakpoints.hpp"

#include <QDir>
#include <QFileInfo>
#include <QSet>

namespace lvd::gui {

QString absoluteSourcePath(const QString& file) {
    if (file.isEmpty())
        return {};
    return QDir::cleanPath(QFileInfo(file).absoluteFilePath());
}

bool sameSourceFile(const QString& a, const QString& b) {
    if (a.isEmpty() || b.isEmpty())
        return false;
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    return absoluteSourcePath(a).compare(absoluteSourcePath(b), cs) == 0;
}

int LineBreakpointList::indexOf(const QString& file, int line) const {
    for (int i = 0; i < size(); ++i)
        if (items_[i].line == line && sameSourceFile(items_[i].file, file))
            return i;
    return -1;
}

int LineBreakpointList::indexOfId(int id) const {
    for (int i = 0; i < size(); ++i)
        if (items_[i].ids.contains(id))
            return i;
    return -1;
}

int LineBreakpointList::add(const QString& file, int line, bool enabled) {
    const int existing = indexOf(file, line);
    if (existing >= 0)
        return existing;
    LineBreakpoint b;
    b.file = absoluteSourcePath(file);
    b.line = line;
    b.enabled = enabled;
    items_.push_back(b);
    return size() - 1;
}

bool LineBreakpointList::remove(const QString& file, int line) {
    const int i = indexOf(file, line);
    if (i < 0)
        return false;
    items_.remove(i);
    return true;
}

QVector<LineBreakpoint> LineBreakpointList::inFile(const QString& file) const {
    QVector<LineBreakpoint> out;
    for (const LineBreakpoint& b : items_)
        if (sameSourceFile(b.file, file))
            out.push_back(b);
    return out;
}

void LineBreakpointList::resetKernelState() {
    for (LineBreakpoint& b : items_) {
        b.state = LineBreakpoint::State::Planned;
        b.ids.clear();
        b.hits = 0;
        b.note.clear();
    }
}

void LineBreakpointList::mergeKernelList(const QVector<LineBreakpointInfo>& list,
                                         const std::function<QString(const QString&)>& resolve) {
    QSet<int> listed;
    QVector<int> hits(size(), 0);
    for (const LineBreakpointInfo& k : list) {
        listed.insert(k.id);
        int i = indexOfId(k.id);
        if (i < 0) {
            // Added in the shell: join the breakpoint on that line, or a new entry.
            const QString file = resolve ? resolve(k.file) : absoluteSourcePath(k.file);
            i = indexOf(file, k.line);
            if (i < 0) {
                i = add(file, k.line);
                hits.push_back(0);
            }
            LineBreakpoint& b = items_[i];
            b.ids.push_back(k.id);
            b.enabled = true;
            b.state = LineBreakpoint::State::Verified;
            b.note.clear();
        }
        hits[i] += k.hits;
    }
    for (int i = size() - 1; i >= 0; --i) {
        LineBreakpoint& b = items_[i];
        if (b.state != LineBreakpoint::State::Verified)
            continue;
        QVector<int> kept;
        for (int id : b.ids)
            if (listed.contains(id))
                kept.push_back(id);
        if (kept.isEmpty()) {
            items_.remove(i); // deleted in the shell
            continue;
        }
        b.ids = kept;
        b.hits = hits[i];
    }
}

QStringList LineBreakpointList::serialize() const {
    QStringList out;
    for (const LineBreakpoint& b : items_)
        out << QStringLiteral("%1|%2|%3").arg(b.enabled ? 1 : 0).arg(b.line).arg(b.file);
    return out;
}

LineBreakpointList LineBreakpointList::deserialize(const QStringList& entries) {
    LineBreakpointList l;
    for (const QString& e : entries) {
        const qsizetype a = e.indexOf(QLatin1Char('|'));
        const qsizetype b = a < 0 ? -1 : e.indexOf(QLatin1Char('|'), a + 1);
        if (b < 0)
            continue;
        bool ok = false;
        const int line = e.mid(a + 1, b - a - 1).toInt(&ok);
        const QString file = e.mid(b + 1);
        if (!ok || line <= 0 || file.isEmpty())
            continue;
        l.add(file, line, e.left(a) != QLatin1String("0"));
    }
    return l;
}

} // namespace lvd::gui
