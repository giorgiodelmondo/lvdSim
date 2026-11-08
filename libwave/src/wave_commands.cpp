// SPDX-License-Identifier: GPL-3.0-or-later
#include <lvdwave/wave_commands.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <cstdio>
#include <set>

namespace lvd::wave {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Case-insensitive glob with * and ?
bool globMatch(const std::string& pattern, const std::string& text) {
    const std::string p = lower(pattern), t = lower(text);
    std::size_t pi = 0, ti = 0, star = std::string::npos, mark = 0;
    while (ti < t.size()) {
        if (pi < p.size() && (p[pi] == '?' || p[pi] == t[ti])) {
            ++pi;
            ++ti;
        } else if (pi < p.size() && p[pi] == '*') {
            star = pi++;
            mark = ti;
        } else if (star != std::string::npos) {
            pi = star + 1;
            ti = ++mark;
        } else {
            return false;
        }
    }
    while (pi < p.size() && p[pi] == '*')
        ++pi;
    return pi == p.size();
}

std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> segs;
    std::size_t pos = 0;
    while (pos <= path.size()) {
        const std::size_t e = path.find('/', pos);
        const std::string seg = path.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        if (!seg.empty())
            segs.push_back(seg);
        if (e == std::string::npos)
            break;
        pos = e + 1;
    }
    return segs;
}

// "count[7:0]" -> "count". Only a trailing bracket group that holds a range (with a colon) is removed,
// so the element "data[3]" keeps its name.
std::string withoutRange(const std::string& name) {
    if (name.size() < 4 || name.back() != ']')
        return name;
    const std::size_t open = name.rfind('[');
    if (open == std::string::npos || open == 0 || name.find(':', open) == std::string::npos)
        return name;
    return name.substr(0, open);
}

struct Matcher {
    const WaveStore& store;
    bool recursive;
    std::vector<SignalId> out;
    std::set<SignalId> seen;

    void add(SignalId id) {
        if (seen.insert(id).second)
            out.push_back(id);
    }

    void signalsIn(ScopeId scope, const std::string& glob, bool descend) {
        for (SignalId id : store.scope(scope).signalIds) {
            const std::string& name = store.entry(id).name();
            // NVC names a vector "count[7:0]"; scripts say "count"
            if (globMatch(glob, name) || globMatch(glob, withoutRange(name)))
                add(id);
        }
        if (descend)
            for (ScopeId c : store.scope(scope).children)
                signalsIn(c, glob, true);
    }

    // segs[idx] is the next component to match below `scope`
    void walk(ScopeId scope, const std::vector<std::string>& segs, std::size_t idx) {
        if (idx + 1 == segs.size()) {
            signalsIn(scope, segs[idx], recursive);
            return;
        }
        for (ScopeId c : store.scope(scope).children)
            if (globMatch(segs[idx], store.scope(c).name))
                walk(c, segs, idx + 1);
    }
};

} // namespace

std::vector<std::string> parseTclList(std::string_view text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    const std::size_t n = text.size();
    while (i < n) {
        while (i < n && std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        if (i >= n)
            break;

        std::string word;
        if (text[i] == '{') {
            int depth = 1;
            for (++i; i < n; ++i) {
                if (text[i] == '{')
                    ++depth;
                else if (text[i] == '}' && --depth == 0) {
                    ++i;
                    break;
                }
                word += text[i];
            }
        } else if (text[i] == '"') {
            for (++i; i < n && text[i] != '"'; ++i) {
                if (text[i] == '\\' && i + 1 < n)
                    ++i;
                word += text[i];
            }
            if (i < n)
                ++i;
        } else {
            while (i < n && !std::isspace(static_cast<unsigned char>(text[i]))) {
                if (text[i] == '\\' && i + 1 < n)
                    ++i;
                word += text[i++];
            }
        }
        out.push_back(std::move(word));
    }
    return out;
}

std::optional<std::uint32_t> parseColor(std::string_view text) {
    const std::string t = lower(std::string(text));
    static const struct {
        const char* name;
        std::uint32_t rgb;
    } kNames[] = {{"red", 0xff6060},    {"green", 0x40e040}, {"blue", 0x7090ff}, {"yellow", 0xffe040},
                  {"orange", 0xffa030}, {"white", 0xf0f0f0}, {"cyan", 0x40d0e0}, {"magenta", 0xff70ff},
                  {"gray", 0x909090},   {"grey", 0x909090},  {"pink", 0xff90c0}};
    for (const auto& e : kNames)
        if (t == e.name)
            return e.rgb;
    if (t.size() == 7 && t[0] == '#') {
        char* end = nullptr;
        const unsigned long v = std::strtoul(t.c_str() + 1, &end, 16);
        if (end != nullptr && *end == '\0')
            return static_cast<std::uint32_t>(v);
    }
    return std::nullopt;
}

Result<AddWaveRequest> parseAddWave(const std::vector<std::string>& words) {
    using R = Result<AddWaveRequest>;
    AddWaveRequest req;
    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::string& w = words[i];
        auto value = [&](const char* opt, std::string& dst) -> bool {
            if (i + 1 >= words.size())
                return false;
            dst = words[++i];
            (void)opt;
            return true;
        };

        if (w.size() > 1 && w[0] == '-' && !std::isdigit(static_cast<unsigned char>(w[1]))) {
            std::string v;
            if (w == "-r" || w == "-recursive") {
                req.recursive = true;
            } else if (w == "-radix") {
                if (!value("-radix", v))
                    return R::failure("add wave: -radix needs a value");
                Radix r;
                if (!parseRadix(v, r))
                    return R::failure("add wave: unknown radix '" + v + "'");
                req.radix = r;
            } else if (w == "-hex" || w == "-hexadecimal" || w == "-binary" || w == "-bin" || w == "-octal" ||
                       w == "-decimal" || w == "-unsigned" || w == "-signed" || w == "-ascii") {
                Radix r = Radix::Default;
                parseRadix(w.substr(1), r);
                req.radix = r;
            } else if (w == "-color") {
                if (!value("-color", v))
                    return R::failure("add wave: -color needs a value");
                const auto c = parseColor(v);
                if (!c)
                    return R::failure("add wave: unknown color '" + v + "'");
                req.color = c;
            } else if (w == "-label" || w == "-name") {
                if (!value("-label", req.label))
                    return R::failure("add wave: -label needs a value");
            } else if (w == "-group") {
                if (!value("-group", req.group))
                    return R::failure("add wave: -group needs a value");
            } else if (w == "-divider") {
                std::string text;
                if (!value("-divider", text))
                    return R::failure("add wave: -divider needs a text");
                req.divider = text;
            } else if (w == "-format") {
                if (!value("-format", v))
                    return R::failure("add wave: -format needs a value");
                if (lower(v).rfind("analog", 0) == 0) {
                    req.analog = true;
                    if (lower(v).find("interpolated") != std::string::npos || lower(v).find("linear") != std::string::npos)
                        req.linear = true;
                }
            } else if (w == "-min" || w == "-max") {
                if (!value(w.c_str(), v))
                    return R::failure("add wave: " + w + " needs a value");
                (w == "-min" ? req.min : req.max) = std::strtod(v.c_str(), nullptr);
            } else if (w == "-height") {
                if (!value("-height", v))
                    return R::failure("add wave: -height needs a value");
                req.heightRows =
                    std::max(1, static_cast<int>((std::atoi(v.c_str()) + 21) / 22)); // pixels to rows
            } else if (w == "-vbus") {
                std::string label;
                if (!value("-vbus", label))
                    return R::failure("add wave: -vbus needs a label");
                req.vbus = label;
            } else if (w == "-collapse") {
                req.collapse = true;
            } else if (w == "-bits") {
                req.showBits = true;
            } else if (w == "-position" || w == "-offset" || w == "-scale" || w == "-expand" ||
                       w == "-collapse" || w == "-noupdate" || w == "-in" || w == "-out" || w == "-inout" ||
                       w == "-internal" || w == "-ports" || w == "-time") {
                if (w == "-position" || w == "-offset" || w == "-scale" || w == "-time")
                    ++i; // these take a value
            }
            // any other flag is skipped
            continue;
        }
        req.patterns.push_back(w);
    }
    if (req.patterns.empty() && !req.divider && req.group.empty())
        return R::failure("add wave: no signals given");
    return req;
}

std::vector<std::string> expandWavePattern(const WaveStore& store, const std::string& pattern,
                                           bool recursive) {
    const std::vector<std::string> segs = splitPath(pattern);
    std::vector<std::string> result;
    if (segs.empty() || store.scope(kRootScope).children.empty())
        return result;

    Matcher m{store, recursive, {}, {}};
    const ScopeId top = store.scope(kRootScope).children.front();

    m.walk(top, segs, 0);   // relative to the design top, as the NVC shell does
    if (segs.size() >= 2) { // starting with the design name, as common simulator scripts do
        for (ScopeId c : store.scope(kRootScope).children)
            if (globMatch(segs[0], store.scope(c).name))
                m.walk(c, segs, 1);
    }
    for (SignalId id : m.out)
        result.push_back(store.pathOf(id));
    return result;
}

namespace {

// The group a request puts its rows into: the model root, or the group named by a path ("outer/inner", one
// level per '/') created on demand. Only a group created by this request starts collapsed, and only the
// innermost one; outer levels created on the way start expanded.
DisplayItem* targetFor(DisplayModel& model, const AddWaveRequest& req) {
    if (req.group.empty())
        return &model.root();
    DisplayItem* cur = &model.root();
    std::size_t pos = 0;
    while (pos <= req.group.size()) {
        std::size_t slash = req.group.find('/', pos);
        if (slash == std::string::npos)
            slash = req.group.size();
        const std::string name = req.group.substr(pos, slash - pos);
        const bool last = slash >= req.group.size();
        pos = slash + 1;
        if (name.empty())
            continue;
        DisplayItem* found = nullptr;
        for (DisplayItem& c : cur->children)
            if (c.kind == ItemKind::Group && c.label == name) {
                found = &c;
                break;
            }
        if (found == nullptr) {
            cur->children.push_back(DisplayModel::makeGroup(name, !(last && req.collapse)));
            found = &cur->children.back();
        }
        cur = found;
    }
    return cur;
}

}  // namespace

std::size_t applyAddWave(DisplayModel& model, const WaveStore& store, const AddWaveRequest& req) {
    if (!req.divider && !req.vbus && req.patterns.empty() && !req.group.empty()) {
        targetFor(model, req);  // `add wave -group NAME`: just the (possibly empty) group
        return 1;
    }
    if (req.divider) {
        targetFor(model, req)->children.push_back(DisplayModel::makeDivider(*req.divider));
        return 1;
    }

    if (req.vbus) {
        std::vector<BusMember> members;
        for (const std::string& pattern : req.patterns) {
            std::string base = pattern;
            int bit = -1;
            const std::size_t at = pattern.rfind('@');
            if (at != std::string::npos && at + 1 < pattern.size() &&
                std::all_of(pattern.begin() + static_cast<std::ptrdiff_t>(at) + 1, pattern.end(),
                            [](unsigned char c) { return std::isdigit(c); })) {
                base = pattern.substr(0, at);
                bit = std::atoi(pattern.c_str() + at + 1);
            }
            for (std::string& p : expandWavePattern(store, base, req.recursive))
                members.push_back(BusMember{std::move(p), bit});
        }
        if (members.empty())
            return 0;
        DisplayItem bus = DisplayModel::makeBus(*req.vbus, std::move(members), req.radix.value_or(Radix::Hex));
        if (req.color)
            bus.color = req.color;
        targetFor(model, req)->children.push_back(std::move(bus));
        return 1;
    }

    std::vector<std::string> paths;
    for (const std::string& pattern : req.patterns)
        for (std::string& p : expandWavePattern(store, pattern, req.recursive))
            paths.push_back(std::move(p));
    if (paths.empty())
        return 0;

    std::vector<DisplayItem> items;
    for (const std::string& p : paths) {
        DisplayItem it = DisplayModel::makeSignal(p, req.radix.value_or(Radix::Default));
        if (req.color)
            it.color = req.color;
        it.showBits = req.showBits;
        if (req.analog) {
            it.analog.enabled = true;
            if (req.min || req.max) {
                // A manual range needs both limits and Min < Max; anything else keeps the automatic range.
                const double lo = req.min.value_or(0.0), hi = req.max.value_or(1.0);
                if (analogRangeError(lo, hi).empty()) {
                    it.analog.autoRange = false;
                    it.analog.min = lo;
                    it.analog.max = hi;
                }
            }
            if (req.heightRows > 0)
                it.analog.heightRows = req.heightRows;
            if (req.linear)
                it.analog.interpolation = Interpolation::Linear;
        }
        items.push_back(std::move(it));
    }
    if (items.size() == 1 && !req.label.empty())
        items.front().label = req.label;

    DisplayItem* target = targetFor(model, req);
    const std::size_t n = items.size();
    for (DisplayItem& it : items)
        target->children.push_back(std::move(it));
    return n;
}

// ---------------------------------------------------------------------------------------------
// .do scripts

namespace {

std::string tclWord(const std::string& s) {
    if (s.empty())
        return "{}";
    for (unsigned char c : s)
        if (std::isspace(c) || std::string("{}[]$\";\\").find(static_cast<char>(c)) != std::string::npos)
            return "{" + s + "}";
    return s;
}

std::string classicPath(const std::string& dotted) {
    std::string p = "/";
    for (char c : dotted)
        p += c == '.' ? '/' : c;
    return p;
}

std::string colorOption(const DisplayItem& it) {
    if (!it.color)
        return {};
    char buf[32];
    std::snprintf(buf, sizeof buf, " -color #%06x", *it.color & 0xFFFFFFu);
    return buf;
}

struct Exporter {
    std::ostringstream os;

    // " -group PATH" plus " -collapse" on the line that first mentions a collapsed group.
    static std::string groupOptions(const std::string& group, bool* pendingCollapse) {
        if (group.empty())
            return {};
        std::string o = " -group " + tclWord(group);
        if (pendingCollapse != nullptr && *pendingCollapse) {
            o += " -collapse";
            *pendingCollapse = false;
        }
        return o;
    }

    // `group` is the path of the enclosing group ("outer/inner"), empty at the top level. `pendingCollapse`
    // is true while the line that creates a collapsed group has not been written.
    void item(const DisplayItem& it, const std::string& group, bool* pendingCollapse) {
        const std::string g = groupOptions(group, pendingCollapse);
        switch (it.kind) {
            case ItemKind::Group: {
                const std::string name = group.empty() ? it.label : group + "/" + it.label;
                // A group announces itself with its first row. One that is empty, or starts with another
                // group, gets a line of its own: `add wave -group NAME [-collapse]`.
                bool pending = !it.expanded;
                if (it.children.empty() || it.children.front().kind == ItemKind::Group) {
                    os << "add wave" << groupOptions(name, &pending) << "\n";
                    pending = false;
                }
                for (const DisplayItem& c : it.children)
                    item(c, name, &pending);
                return;
            }
            case ItemKind::Divider:
                os << "add wave" << g << " -divider " << tclWord(it.label) << "\n";
                return;
            case ItemKind::VirtualBus:
                os << "add wave -vbus " << tclWord(it.label) << g;
                if (it.radix != Radix::Hex)  // hex is what a bus gets when no radix is given
                    os << " -radix " << radixName(it.radix);
                os << colorOption(it);
                for (const BusMember& m : it.members)
                    os << ' ' << tclWord(classicPath(m.path) + (m.bit >= 0 ? "@" + std::to_string(m.bit) : ""));
                os << "\n";
                return;
            case ItemKind::Signal:
                os << "add wave" << g;
                if (it.radix != Radix::Default)
                    os << " -radix " << radixName(it.radix);
                os << colorOption(it);
                if (!it.label.empty())
                    os << " -label " << tclWord(it.label);
                if (it.showBits)
                    os << " -bits";
                if (it.analog.enabled) {
                    os << " -format " << (it.analog.interpolation == Interpolation::Linear ? "analog-interpolated" : "analog-step");
                    if (!it.analog.autoRange) {
                        char buf[96];
                        std::snprintf(buf, sizeof buf, " -min %.17g -max %.17g", it.analog.min, it.analog.max);
                        os << buf;
                    }
                    os << " -height " << it.analog.heightRows * 22;
                }
                os << ' ' << tclWord(classicPath(it.path)) << "\n";
                return;
        }
    }
};

}  // namespace

std::string exportDoScript(const DisplayModel& model) {
    Exporter e;
    e.os << "# lvdsim wave layout: familiar add wave commands (virtual bus, group collapse, show-bits, nested group paths and empty groups are lvdsim extensions)\n";
    for (const DisplayItem& it : model.root().children)
        e.item(it, {}, nullptr);
    return e.os.str();
}

ImportResult importDoScript(std::string_view script, DisplayModel& model, const WaveStore& store) {
    ImportResult res;
    // Logical lines (a trailing backslash continues the line) with the number of the physical line they start on.
    std::vector<std::pair<int, std::string>> lines;
    {
        std::string cur;
        int startLine = 1, physical = 0;
        std::size_t pos = 0;
        while (pos <= script.size()) {
            ++physical;
            const std::size_t nl = script.find('\n', pos);
            std::string line(script.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos));
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (cur.empty())
                startLine = physical;
            if (!line.empty() && line.back() == '\\') {
                line.pop_back();
                cur += line + " ";
            } else {
                lines.emplace_back(startLine, cur + line);
                cur.clear();
            }
            if (nl == std::string_view::npos)
                break;
            pos = nl + 1;
        }
        if (!cur.empty())
            lines.emplace_back(startLine, cur);
    }

    for (const auto& [lineNo, raw] : lines) {
        const std::vector<std::string> words = parseTclList(raw);
        if (words.empty() || words[0].empty() || words[0][0] == '#')
            continue;
        if (words.size() < 2 || words[0] != "add" || words[1] != "wave") {
            ++res.skipped;
            continue;
        }
        const auto req = parseAddWave(std::vector<std::string>(words.begin() + 2, words.end()));
        if (!req.ok()) {
            res.errors.push_back("line " + std::to_string(lineNo) + ": " + req.error());
            continue;
        }
        const std::size_t n = applyAddWave(model, store, req.value());
        if (n == 0)
            res.errors.push_back("line " + std::to_string(lineNo) + ": no signal matches");
        res.rowsAdded += n;
    }
    return res;
}

} // namespace lvd::wave
