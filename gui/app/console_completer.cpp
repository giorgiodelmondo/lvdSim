// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/console_completer.hpp"

#include <lvdwave/store.hpp>

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <vector>

namespace lvd::gui {

using lvd::wave::ScopeId;
using lvd::wave::WaveStore;

namespace {

#ifdef Q_OS_WIN
constexpr Qt::CaseSensitivity kFileCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity kFileCase = Qt::CaseSensitive;
#endif

QStringList words(const char* text) {
    return QString::fromLatin1(text).split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

const QHash<QString, QStringList>& optionTable() {
    static const QHash<QString, QStringList> t = [] {
        QHash<QString, QStringList> h;
        const QStringList wave =
            words("-ascii -binary -bits -collapse -color -compare -decimal -divider -expr "
                  "-format -group -height -hexadecimal -in -inout -internal -label -max -min "
                  "-octal -out -ports -r -radix -recursive -signed -tolerance -unsigned -vbus");
        h.insert("run", words("-all -continue -json"));
        h.insert("step", words("-command -delta -into -json -out -over -time"));
        h.insert("add wave", wave);
        h.insert("add list", wave);
        h.insert("add mem", words("-addressradix -dataradix -radix -wordsperline"));
        h.insert("force", words("-cancel -deposit -drive -freeze -repeat"));
        h.insert("examine", words("-binary -decimal -hexadecimal -json -radix"));
        h.insert("exa", h.value("examine"));
        h.insert("find signals", words("-json -r"));
        h.insert("find regions", words("-json -r"));
        h.insert("describe", words("-json -location"));
        h.insert("processes", words("-json"));
        h.insert("breakpoint add", words("-command -json"));
        h.insert("breakpoint delete", words("-json"));
        h.insert("breakpoint list", words("-json"));
        h.insert("stack", words("-json"));
        h.insert("locals", words("-frame -json"));
        h.insert("quit", words("-code -f -force -sim"));
        h.insert("exit", words("-code -force"));
        h.insert("restart", words("-f -force"));
        h.insert("vcom",
                 words("-2002 -2008 -2019 -87 -93 -L -O0 -O1 -O2 -O3 -check_synthesis -explicit -lint "
                       "-nocheck -nologo -nowarn -novopt -pedanticerrors -quiet -suppress -work"));
        h.insert("vlog", words("-L -O0 -O1 -O2 -O3 -incr -lint -mfcu -nologo -nowarn -novopt -pedanticerrors "
                               "-quiet -suppress -sv -timescale -v2k5 -work"));
        h.insert("vlib", words("-dos -short -type -unix"));
        h.insert("vmap", words("-c -del"));
        return h;
    }();
    return t;
}

const QHash<QString, QStringList>& subcommandTable() {
    static const QHash<QString, QStringList> t = {
        {"add", words("list mem wave")},
        {"delete", words("list wave")},
        {"find", words("regions signals")},
        {"breakpoint", words("add delete list")},
        {"view", words("breakpoints library list locals memory objects processes source stack structure "
                       "transcript wave")},
    };
    return t;
}

/// Commands whose plain words are design paths.
bool takesPaths(const QString& key) {
    static const QSet<QString> s = {"add wave", "add list",     "add mem",     "examine",
                                    "exa",      "force",        "noforce",     "deposit",
                                    "describe", "find signals", "find regions"};
    return s.contains(key);
}

/// Commands whose plain words are file names, with the extensions offered.
QStringList fileExtensions(const QString& command) {
    if (command == "do" || command == "source")
        return {"do", "tcl", "udo"};
    if (command == "vcom")
        return {"vhd", "vhdl", "vho"};
    if (command == "vlog")
        return {"v", "sv", "svh", "vh", "svi"};
    if (command == "readmem")
        return {"mem", "hex", "txt", "dat"};
    return {};
}

/// The command and words before the cursor, and the word being typed.
struct Context {
    QStringList words; ///< whole words of the current command before the word being typed
    int start = 0;     ///< where the typed word starts
    QString word;
};

Context contextAt(const QString& t, int cursor) {
    std::vector<QStringList> levels(1);
    QString cur;
    int curStart = -1;
    int brace = 0;
    bool quote = false;
    auto endWord = [&] {
        if (curStart >= 0)
            levels.back() << cur;
        cur.clear();
        curStart = -1;
    };
    for (int i = 0; i < cursor; ++i) {
        const QChar c = t[i];
        if (c == QLatin1Char('\\')) {
            if (curStart < 0)
                curStart = i;
            cur += c;
            if (i + 1 < cursor)
                cur += t[++i];
            continue;
        }
        if (brace > 0) {
            if (c == QLatin1Char('{'))
                ++brace;
            else if (c == QLatin1Char('}'))
                --brace;
            cur += c;
            continue;
        }
        if (quote) {
            if (c == QLatin1Char('"'))
                quote = false;
            cur += c;
            continue;
        }
        if (c == QLatin1Char(' ') || c == QLatin1Char('\t')) {
            endWord();
        } else if (c == QLatin1Char('\n') || c == QLatin1Char(';')) {
            endWord();
            levels.back().clear();
        } else if (c == QLatin1Char('[')) {
            cur.clear();
            curStart = -1;
            levels.emplace_back();
        } else if (c == QLatin1Char(']') && levels.size() > 1) {
            levels.pop_back();
            cur = QStringLiteral("[]");
            curStart = i;
        } else {
            if (curStart < 0) {
                curStart = i;
                if (c == QLatin1Char('{'))
                    brace = 1;
                else if (c == QLatin1Char('"'))
                    quote = true;
            }
            cur += c;
        }
    }
    Context ctx;
    ctx.words = levels.back();
    if (curStart < 0) {
        ctx.start = cursor;
    } else if (brace > 0 || quote) {
        // Inside an open brace or quote: complete the part after the last blank or the opening character.
        int s = cursor;
        while (s > curStart && t[s - 1] != QLatin1Char(' ') && t[s - 1] != QLatin1Char('\t') &&
               !(s - 1 == curStart && (t[s - 1] == QLatin1Char('{') || t[s - 1] == QLatin1Char('"'))))
            --s;
        ctx.start = s;
    } else {
        ctx.start = curStart;
    }
    ctx.word = t.mid(ctx.start, cursor - ctx.start);
    return ctx;
}

void filterInto(const QStringList& from, const QString& prefix, Qt::CaseSensitivity cs, QStringList* out) {
    for (const QString& s : from)
        if (s.startsWith(prefix, cs))
            *out << s;
}

} // namespace

QString Completion::commonPrefix() const {
    if (candidates.isEmpty())
        return prefix;
    const Qt::CaseSensitivity cs =
        (kind == Kind::Path || kind == Kind::File) ? Qt::CaseInsensitive : Qt::CaseSensitive;
    QString common = candidates.first();
    for (const QString& c : candidates) {
        qsizetype n = 0;
        const qsizetype m = qMin(common.size(), c.size());
        while (n < m && QStringView(common).mid(n, 1).compare(QStringView(c).mid(n, 1), cs) == 0)
            ++n;
        common.truncate(n);
    }
    return common.size() >= prefix.size() ? common : prefix;
}

ConsoleCompleter::ConsoleCompleter() {
    commands_ = builtinCommands();
}

QStringList ConsoleCompleter::builtinCommands() {
    // The native shell (nvc -i), the command compatibility layer (tcl/compat) and what the GUI runs itself.
    QStringList l =
        words("help exit copyright find run step breakpoint stack locals restart examine exa force "
              "noforce deposit readmem echo describe processes "
              "add delete log wave view radix configure transcript onerror onbreak onElabError "
              "quietly do when bp vcd quit "
              "vlib vmap vcom vlog "
              "after append array break catch cd clock close concat continue dict encoding eof error "
              "eval expr fconfigure file flush for foreach format gets glob global if incr info join "
              "lappend lassign lindex linsert list llength lmap lrange lrepeat lreplace lreverse "
              "lsearch lset lsort namespace open proc puts pwd read regexp regsub rename return scan "
              "seek set source split string subst switch tell time trace unset update uplevel upvar "
              "variable vwait while");
    l.sort();
    l.removeDuplicates();
    return l;
}

QStringList ConsoleCompleter::optionsFor(const QString& commandKey) {
    QStringList l = optionTable().value(commandKey);
    l.sort();
    return l;
}

QStringList ConsoleCompleter::subcommandsFor(const QString& command) {
    return subcommandTable().value(command);
}

void ConsoleCompleter::setShellCommands(const QStringList& commands) {
    QStringList l = builtinCommands();
    for (const QString& c : commands)
        if (!c.isEmpty() && !c.startsWith(QLatin1String("::")))
            l << c;
    l.sort();
    l.removeDuplicates();
    commands_ = l;
    haveShell_ = true;
}

Completion ConsoleCompleter::complete(const QString& text, int cursor) const {
    cursor = qBound(0, cursor, static_cast<int>(text.size()));
    const Context ctx = contextAt(text, cursor);
    Completion r;
    r.start = ctx.start;
    r.prefix = ctx.word;
    const QString& w = ctx.word;

    if (ctx.words.isEmpty()) {
        r.kind = Completion::Kind::Command;
        filterInto(commands_, w, Qt::CaseSensitive, &r.candidates);
        r.labels = r.candidates;
        return r;
    }

    const QString cmd = ctx.words.first();
    QString key = cmd;
    if (ctx.words.size() >= 2 && !subcommandsFor(cmd).isEmpty())
        key = cmd + QLatin1Char(' ') + ctx.words.at(1);
    if (key == "add memory")
        key = QStringLiteral("add mem");
    const QString previous = ctx.words.last();

    if (previous == QLatin1String("-radix") || previous == QLatin1String("-addressradix") ||
        previous == QLatin1String("-dataradix")) {
        r.kind = Completion::Kind::Value;
        filterInto(words("ascii binary decimal hexadecimal octal signed unsigned"), w, Qt::CaseInsensitive,
                   &r.candidates);
    } else if (w.startsWith(QLatin1Char('-'))) {
        r.kind = Completion::Kind::Option;
        filterInto(optionsFor(key), w, Qt::CaseSensitive, &r.candidates);
    } else if (ctx.words.size() == 1 && !subcommandsFor(cmd).isEmpty()) {
        r.kind = Completion::Kind::Subcommand;
        filterInto(subcommandsFor(cmd), w, Qt::CaseSensitive, &r.candidates);
    } else if (w.startsWith(QLatin1Char('/')) || (w.isEmpty() && takesPaths(key))) {
        r.kind = Completion::Kind::Path;
        r.candidates = completePath(w.isEmpty() ? QStringLiteral("/") : w, &r.labels);
        return r;
    } else if (!fileExtensions(cmd).isEmpty()) {
        r.kind = Completion::Kind::File;
        r.candidates = completeFile(w, fileExtensions(cmd), &r.labels);
        return r;
    }
    r.labels = r.candidates;
    return r;
}

QStringList ConsoleCompleter::completePath(const QString& prefix, QStringList* labels) const {
    QStringList out;
    if (!store_ || store_->scopeCount() == 0 || !prefix.startsWith(QLatin1Char('/')))
        return out;
    // The shell names objects relative to the design top: "/" is the top instance when there is one.
    ScopeId scope = lvd::wave::kRootScope;
    {
        const lvd::wave::Scope& root = store_->scope(lvd::wave::kRootScope);
        if (root.children.size() == 1 && root.signalIds.empty())
            scope = root.children.front();
    }
    QStringList segs = prefix.mid(1).split(QLatin1Char('/'));
    const QString last = segs.takeLast();
    QString base = QStringLiteral("/");
    for (const QString& s : segs) {
        ScopeId next = lvd::wave::kNoScope;
        for (ScopeId c : store_->scope(scope).children)
            if (QString::fromStdString(store_->scope(c).name).compare(s, Qt::CaseInsensitive) == 0) {
                next = c;
                break;
            }
        if (next == lvd::wave::kNoScope)
            return out;
        scope = next;
        base += QString::fromStdString(store_->scope(scope).name) + QLatin1Char('/');
    }
    struct Item {
        QString name;
        bool container;
    };
    std::vector<Item> items;
    QSet<QString> seen;
    const lvd::wave::Scope& sc = store_->scope(scope);
    for (ScopeId c : sc.children) {
        const QString n = QString::fromStdString(store_->scope(c).name);
        if (n.startsWith(last, Qt::CaseInsensitive) && !seen.contains(n + '/')) {
            seen.insert(n + '/');
            items.push_back({n, true});
        }
    }
    for (lvd::wave::SignalId id : sc.signalIds) {
        const QString n = QString::fromStdString(store_->entry(id).name());
        if (n.startsWith(last, Qt::CaseInsensitive) && !seen.contains(n)) {
            seen.insert(n);
            items.push_back({n, false});
        }
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        const int c = QString::compare(a.name, b.name, Qt::CaseInsensitive);
        return c != 0 ? c < 0 : a.container > b.container;
    });
    for (const Item& it : items) {
        const QString label = it.container ? it.name + QLatin1Char('/') : it.name;
        out << base + label;
        if (labels)
            *labels << label;
    }
    return out;
}

QStringList ConsoleCompleter::completeFile(const QString& prefix, const QStringList& extensions,
                                           QStringList* labels) const {
    QStringList out;
    const QString p = QDir::fromNativeSeparators(prefix);
    const qsizetype slash = p.lastIndexOf(QLatin1Char('/'));
    const QString dirPart = slash >= 0 ? p.left(slash + 1) : QString();
    const QString namePart = p.mid(slash + 1);
    const QString baseDir = workingDir_.isEmpty() ? QDir::currentPath() : workingDir_;
    QDir dir(dirPart.isEmpty() ? baseDir
                               : (QDir::isAbsolutePath(dirPart) ? dirPart : baseDir + '/' + dirPart));
    if (!dir.exists())
        return out;
    const QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot,
                                                    QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& fi : entries) {
        if (!fi.fileName().startsWith(namePart, kFileCase))
            continue;
        if (!fi.isDir() && !extensions.contains(fi.suffix(), Qt::CaseInsensitive))
            continue;
        const QString label = fi.isDir() ? fi.fileName() + QLatin1Char('/') : fi.fileName();
        out << dirPart + label;
        if (labels)
            *labels << label;
        if (out.size() >= 500)
            break;
    }
    return out;
}

} // namespace lvd::gui
