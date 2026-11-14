// SPDX-License-Identifier: GPL-3.0-or-later
#include <lvdwave/display_model.hpp>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace lvd::wave {

ItemId nextItemId() noexcept {
    static std::atomic<ItemId> counter{1};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

std::string DisplayItem::displayName() const {
    if (!label.empty())
        return label;
    if (kind == ItemKind::Expression)
        return expr;
    return path;
}

DisplayModel::DisplayModel() {
    root_.kind = ItemKind::Group;
    root_.label = "root";
}

DisplayItem DisplayModel::makeSignal(std::string path, Radix radix) {
    DisplayItem i;
    i.kind = ItemKind::Signal;
    i.path = std::move(path);
    i.radix = radix;
    return i;
}

DisplayItem DisplayModel::makeGroup(std::string label, bool expanded) {
    DisplayItem i;
    i.kind = ItemKind::Group;
    i.label = std::move(label);
    i.expanded = expanded;
    return i;
}

DisplayItem DisplayModel::makeDivider(std::string label) {
    DisplayItem i;
    i.kind = ItemKind::Divider;
    i.label = std::move(label);
    return i;
}

DisplayItem DisplayModel::makeBus(std::string label, std::vector<BusMember> members, Radix radix) {
    DisplayItem i;
    i.kind = ItemKind::VirtualBus;
    i.label = std::move(label);
    i.members = std::move(members);
    i.radix = radix;
    return i;
}

DisplayItem DisplayModel::makeExpression(std::string label, std::string expr, Radix radix) {
    DisplayItem i;
    i.kind = ItemKind::Expression;
    i.label = std::move(label);
    i.expr = std::move(expr);
    i.radix = radix;
    return i;
}

DisplayItem DisplayModel::makeCompare(std::string label, std::string refPath, std::string testPath,
                                      Time tolerance) {
    DisplayItem i;
    i.kind = ItemKind::Compare;
    i.label = std::move(label);
    i.members = {BusMember{std::move(refPath)}, BusMember{std::move(testPath)}};
    i.tolerance = tolerance;
    i.color = 0xff6060u;
    return i;
}

namespace {

void flatten(const DisplayItem& group, int depth, std::vector<DisplayRow>& out) {
    for (const DisplayItem& c : group.children) {
        out.push_back({&c, depth});
        if (c.kind == ItemKind::Group && c.expanded)
            flatten(c, depth + 1, out);
    }
}

std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

void writeItem(const DisplayItem& it, int depth, std::ostringstream& os) {
    const std::string pad(static_cast<std::size_t>(depth) * 2, ' ');
    auto analogText = [](const AnalogSettings& a) {
        std::ostringstream s;
        s << " analog=" << (a.autoRange ? std::string("auto") : [&] {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.17g:%.17g", a.min, a.max);
            return std::string(buf);
        }());
        if (a.interpolation == Interpolation::Linear)
            s << " linear";
        if (a.isSigned)
            s << " signed";
        s << " height=" << a.heightRows;
        return s.str();
    };
    auto colorText = [](const DisplayItem& i) {
        std::string out;
        if (i.color) {
            char buf[16];
            std::snprintf(buf, sizeof buf, " color=#%06x", *i.color & 0xFFFFFFu);
            out = buf;
        }
        if (i.heightPx > 0) // the row height in pixels travels with the other per-row display options
            out += " heightpx=" + std::to_string(i.heightPx);
        return out;
    };

    switch (it.kind) {
    case ItemKind::Signal:
        os << pad << "signal " << quote(it.path) << " radix=" << radixName(it.radix);
        if (!it.label.empty())
            os << " label=" << quote(it.label);
        if (it.showBits)
            os << " bits";
        os << colorText(it);
        if (it.analog.enabled)
            os << analogText(it.analog);
        os << '\n';
        break;
    case ItemKind::Divider:
        os << pad << "divider " << quote(it.label) << colorText(it) << '\n';
        break;
    case ItemKind::VirtualBus:
        os << pad << "bus " << quote(it.label) << " radix=" << radixName(it.radix) << colorText(it);
        if (it.analog.enabled)
            os << analogText(it.analog);
        os << '\n';
        for (const BusMember& m : it.members) {
            os << pad << "  member " << quote(m.path);
            if (m.bit >= 0)
                os << " bit=" << m.bit;
            os << '\n';
        }
        os << pad << "end\n";
        break;
    case ItemKind::Expression:
        os << pad << "expr " << quote(it.label) << ' ' << quote(it.expr) << " radix=" << radixName(it.radix)
           << colorText(it);
        if (it.analog.enabled)
            os << analogText(it.analog);
        os << '\n';
        break;
    case ItemKind::Compare:
        os << pad << "compare " << quote(it.label) << ' '
           << quote(it.members.size() > 0 ? it.members[0].path : std::string()) << ' '
           << quote(it.members.size() > 1 ? it.members[1].path : std::string())
           << " tolerance=" << it.tolerance << colorText(it) << '\n';
        break;
    case ItemKind::Group:
        os << pad << "group " << quote(it.label) << (it.expanded ? " expanded" : " collapsed") << '\n';
        for (const DisplayItem& c : it.children)
            writeItem(c, depth + 1, os);
        os << pad << "end\n";
        break;
    }
}

struct Token {
    std::string key;   // set for key=value tokens
    std::string value; // the word, the quoted text, or the value of key=value
    bool isKeyValue = false;
};

// Splits a line into words, quoted strings and key=value pairs (value may be quoted).
bool tokenize(const std::string& line, std::vector<Token>& out, std::string& error) {
    std::size_t i = 0;
    const std::size_t n = line.size();
    auto readQuoted = [&](std::string& dst) -> bool {
        ++i; // opening quote
        while (i < n && line[i] != '"') {
            if (line[i] == '\\' && i + 1 < n)
                ++i;
            dst += line[i++];
        }
        if (i >= n)
            return false;
        ++i; // closing quote
        return true;
    };

    while (i < n) {
        if (line[i] == ' ' || line[i] == '\t' || line[i] == '\r') {
            ++i;
            continue;
        }
        Token t;
        if (line[i] == '"') {
            if (!readQuoted(t.value)) {
                error = "unterminated string";
                return false;
            }
        } else {
            std::string word;
            while (i < n && line[i] != ' ' && line[i] != '\t' && line[i] != '\r' && line[i] != '"')
                word += line[i++];
            const std::size_t eq = word.find('=');
            if (eq == std::string::npos) {
                t.value = word;
            } else {
                t.isKeyValue = true;
                t.key = word.substr(0, eq);
                t.value = word.substr(eq + 1);
                if (t.value.empty() && i < n && line[i] == '"') {
                    if (!readQuoted(t.value)) {
                        error = "unterminated string";
                        return false;
                    }
                }
            }
        }
        out.push_back(std::move(t));
    }
    return true;
}

bool parseAnalog(const std::string& v, AnalogSettings& a) {
    a.enabled = true;
    if (v == "auto") {
        a.autoRange = true;
        return true;
    }
    const std::size_t c = v.find(':');
    if (c == std::string::npos)
        return false;
    char* end = nullptr;
    a.min = std::strtod(v.substr(0, c).c_str(), &end);
    a.max = std::strtod(v.substr(c + 1).c_str(), &end);
    a.autoRange = false;
    return true;
}

} // namespace

int clampRowHeight(int px) noexcept {
    if (px <= 0)
        return 0;
    return px < kMinRowHeightPx ? kMinRowHeightPx : (px > kMaxRowHeightPx ? kMaxRowHeightPx : px);
}

int rowHeightPx(const DisplayItem& item, bool analogTrace) noexcept {
    if (item.heightPx > 0)
        return clampRowHeight(item.heightPx);
    if (analogTrace)
        return clampRowHeight((item.analog.heightRows < 1 ? 1 : item.analog.heightRows) *
                              kDefaultRowHeightPx);
    return kDefaultRowHeightPx;
}

bool analogSigned(const DisplayItem& item) noexcept {
    return item.analog.isSigned || item.radix == Radix::Signed;
}

std::string analogRangeError(double min, double max) {
    if (!std::isfinite(min) || !std::isfinite(max))
        return "The range limits must be finite numbers.";
    if (!(min < max))
        return "Min must be less than Max.";
    return {};
}

std::optional<AnalogRange> resolveAnalogRange(const AnalogSettings& settings,
                                              std::optional<AnalogRange> dataRange) {
    AnalogRange r;
    if (settings.autoRange) {
        if (!dataRange)
            return std::nullopt;
        r = *dataRange;
    } else {
        r.min = settings.min;
        r.max = settings.max;
    }
    if (!std::isfinite(r.min) || !std::isfinite(r.max))
        r = AnalogRange{0.0, 1.0};
    if (!(r.max > r.min)) {
        const double v = r.min;
        r = AnalogRange{v - 0.5, v + 0.5};
    }
    return r;
}

double analogFraction(const AnalogRange& range, double value) noexcept {
    if (std::isnan(value))
        return 0.0;
    const double f = (value - range.min) / (range.max - range.min);
    return f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
}

std::vector<DisplayRow> DisplayModel::visibleRows() const {
    std::vector<DisplayRow> rows;
    flatten(root_, 0, rows);
    return rows;
}

std::string DisplayModel::serialize() const {
    std::ostringstream os;
    os << "lvdwave-display 1\n";
    for (const DisplayItem& c : root_.children)
        writeItem(c, 0, os);
    return os.str();
}

Result<DisplayModel> DisplayModel::parse(std::string_view text) {
    using R = Result<DisplayModel>;
    DisplayModel model;
    std::vector<DisplayItem*> stack{&model.root_}; // open groups
    DisplayItem* openBus = nullptr;                // bus currently collecting members

    std::istringstream in{std::string(text)};
    std::string line;
    int lineNo = 0;
    bool sawHeader = false;

    auto fail = [&](const std::string& msg) {
        return R::failure("line " + std::to_string(lineNo) + ": " + msg);
    };

    while (std::getline(in, line)) {
        ++lineNo;
        std::vector<Token> toks;
        std::string err;
        if (!tokenize(line, toks, err))
            return fail(err);
        if (toks.empty() || (!toks[0].value.empty() && toks[0].value[0] == '#'))
            continue;

        const std::string& cmd = toks[0].value;
        if (!sawHeader) {
            if (cmd != "lvdwave-display" || toks.size() < 2 || toks[1].value != "1")
                return fail("missing or unsupported header");
            sawHeader = true;
            continue;
        }

        if (openBus != nullptr) {
            if (cmd == "member" && (toks.size() == 2 || toks.size() == 3)) {
                int bit = -1;
                if (toks.size() == 3) {
                    if (!toks[2].isKeyValue || toks[2].key != "bit")
                        return fail("expected bit=N after member path");
                    bit = std::atoi(toks[2].value.c_str());
                }
                openBus->members.push_back(BusMember{toks[1].value, bit});
                continue;
            }
            if (cmd == "end") {
                openBus = nullptr;
                continue;
            }
            return fail("expected member or end inside bus");
        }

        auto applyOptions = [&](DisplayItem& item, std::size_t from) -> std::string {
            for (std::size_t i = from; i < toks.size(); ++i) {
                const Token& t = toks[i];
                if (t.isKeyValue && t.key == "radix") {
                    if (!parseRadix(t.value, item.radix))
                        return "unknown radix " + t.value;
                } else if (t.isKeyValue && t.key == "label") {
                    item.label = t.value;
                } else if (t.isKeyValue && t.key == "analog") {
                    if (!parseAnalog(t.value, item.analog))
                        return "bad analog range " + t.value;
                } else if (t.isKeyValue && t.key == "height") {
                    item.analog.heightRows = std::atoi(t.value.c_str());
                } else if (t.isKeyValue && t.key == "heightpx") {
                    item.heightPx = clampRowHeight(std::atoi(t.value.c_str()));
                } else if (t.isKeyValue && t.key == "color") {
                    if (t.value.size() != 7 || t.value[0] != '#')
                        return "bad colour " + t.value;
                    item.color = static_cast<std::uint32_t>(std::strtoul(t.value.c_str() + 1, nullptr, 16));
                } else if (t.isKeyValue && t.key == "tolerance") {
                    item.tolerance = std::strtoull(t.value.c_str(), nullptr, 10);
                } else if (!t.isKeyValue && t.value == "bits") {
                    item.showBits = true;
                } else if (!t.isKeyValue && t.value == "linear") {
                    item.analog.interpolation = Interpolation::Linear;
                } else if (!t.isKeyValue && t.value == "signed") {
                    item.analog.isSigned = true;
                } else {
                    return "unknown option " + t.value;
                }
            }
            return {};
        };

        DisplayItem& parent = *stack.back();
        if (cmd == "signal" && toks.size() >= 2) {
            DisplayItem it = makeSignal(toks[1].value);
            if (std::string e = applyOptions(it, 2); !e.empty())
                return fail(e);
            parent.children.push_back(std::move(it));
        } else if (cmd == "expr" && toks.size() >= 3 && !toks[1].isKeyValue && !toks[2].isKeyValue) {
            DisplayItem it = makeExpression(toks[1].value, toks[2].value);
            if (std::string e = applyOptions(it, 3); !e.empty())
                return fail(e);
            parent.children.push_back(std::move(it));
        } else if (cmd == "compare" && toks.size() >= 4 && !toks[1].isKeyValue && !toks[2].isKeyValue &&
                   !toks[3].isKeyValue) {
            DisplayItem it = makeCompare(toks[1].value, toks[2].value, toks[3].value);
            it.color.reset(); // only what the file says
            if (std::string e = applyOptions(it, 4); !e.empty())
                return fail(e);
            parent.children.push_back(std::move(it));
        } else if (cmd == "divider" && toks.size() >= 2) {
            DisplayItem it = makeDivider(toks[1].value);
            if (std::string e = applyOptions(it, 2); !e.empty())
                return fail(e);
            parent.children.push_back(std::move(it));
        } else if (cmd == "bus" && toks.size() >= 2) {
            DisplayItem it = makeBus(toks[1].value, {});
            if (std::string e = applyOptions(it, 2); !e.empty())
                return fail(e);
            parent.children.push_back(std::move(it));
            openBus = &parent.children.back();
        } else if (cmd == "group" && toks.size() >= 2) {
            const bool collapsed = toks.size() >= 3 && toks[2].value == "collapsed";
            parent.children.push_back(makeGroup(toks[1].value, !collapsed));
            stack.push_back(&parent.children.back());
        } else if (cmd == "end") {
            if (stack.size() == 1)
                return fail("unbalanced end");
            stack.pop_back();
        } else {
            return fail("unknown statement " + cmd);
        }
    }

    if (!sawHeader)
        return R::failure("empty input: missing header");
    if (openBus != nullptr || stack.size() != 1)
        return R::failure("unterminated group or bus at end of input");
    return model;
}

namespace {

DisplayItem* findParentIn(DisplayItem& group, const DisplayItem* item, std::size_t* indexOut) {
    for (std::size_t i = 0; i < group.children.size(); ++i) {
        if (&group.children[i] == item) {
            if (indexOut != nullptr)
                *indexOut = i;
            return &group;
        }
        if (group.children[i].kind == ItemKind::Group) {
            if (DisplayItem* p = findParentIn(group.children[i], item, indexOut))
                return p;
        }
    }
    return nullptr;
}

// Indices of the selected items in their parent, ascending, or empty if they do not share a parent.
std::vector<std::size_t> sharedParentIndices(DisplayModel& model,
                                             const std::vector<const DisplayItem*>& items,
                                             DisplayItem*& parentOut) {
    parentOut = nullptr;
    std::vector<std::size_t> idx;
    for (const DisplayItem* it : items) {
        std::size_t i = 0;
        DisplayItem* p = findParent(model, it, &i);
        if (p == nullptr || (parentOut != nullptr && p != parentOut))
            return {};
        parentOut = p;
        idx.push_back(i);
    }
    std::sort(idx.begin(), idx.end());
    idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
    return idx;
}

} // namespace

DisplayItem* findParent(DisplayModel& model, const DisplayItem* item, std::size_t* indexOut) {
    return findParentIn(model.root(), item, indexOut);
}

bool groupItems(DisplayModel& model, const std::vector<const DisplayItem*>& items, std::string label) {
    if (items.empty())
        return false;
    DisplayItem* parent = nullptr;
    const std::vector<std::size_t> idx = sharedParentIndices(model, items, parent);
    if (idx.empty())
        return false;

    DisplayItem group = DisplayModel::makeGroup(std::move(label));
    for (std::size_t i : idx)
        group.children.push_back(std::move(parent->children[i]));
    for (auto it = idx.rbegin(); it != idx.rend(); ++it)
        parent->children.erase(parent->children.begin() + static_cast<std::ptrdiff_t>(*it));
    parent->children.insert(parent->children.begin() + static_cast<std::ptrdiff_t>(idx.front()),
                            std::move(group));
    return true;
}

std::size_t removeItems(DisplayModel& model, const std::vector<const DisplayItem*>& items) {
    // Resolve to (parent, index) first because erasing shifts pointers.
    struct Loc {
        DisplayItem* parent;
        std::size_t index;
    };
    std::vector<Loc> locs;
    for (const DisplayItem* it : items) {
        std::size_t i = 0;
        if (DisplayItem* p = findParent(model, it, &i))
            locs.push_back({p, i});
    }
    // Erase from the back of each parent first. Skip items whose ancestor is also being removed.
    std::sort(locs.begin(), locs.end(), [](const Loc& a, const Loc& b) {
        return a.parent != b.parent ? a.parent < b.parent : a.index > b.index;
    });
    std::size_t n = 0;
    for (const Loc& l : locs) {
        l.parent->children.erase(l.parent->children.begin() + static_cast<std::ptrdiff_t>(l.index));
        ++n;
    }
    return n;
}

bool busFromSignals(DisplayModel& model, const std::vector<const DisplayItem*>& items, std::string label,
                    Radix radix) {
    DisplayItem* parent = nullptr;
    const std::vector<std::size_t> idx = sharedParentIndices(model, items, parent);
    if (idx.empty())
        return false;

    std::vector<BusMember> members;
    for (std::size_t i : idx) {
        const DisplayItem& it = parent->children[i];
        if (it.kind == ItemKind::Signal)
            members.push_back(BusMember{it.path, -1});
    }
    if (members.size() < 2)
        return false;

    DisplayItem bus = DisplayModel::makeBus(std::move(label), std::move(members), radix);
    parent->children.insert(parent->children.begin() + static_cast<std::ptrdiff_t>(idx.back()) + 1,
                            std::move(bus));
    return true;
}

void insertAfter(DisplayModel& model, const DisplayItem* after, DisplayItem item) {
    std::size_t i = 0;
    DisplayItem* parent = after != nullptr ? findParent(model, after, &i) : nullptr;
    if (parent == nullptr) {
        model.root().children.push_back(std::move(item));
        return;
    }
    parent->children.insert(parent->children.begin() + static_cast<std::ptrdiff_t>(i) + 1, std::move(item));
}

std::string busValueAt(const WaveStore& store, const std::vector<BusMember>& members, Time t) {
    std::string out;
    for (const BusMember& m : members) {
        const SignalId id = store.findSignal(m.path);
        if (id == kNoSignal) {
            out += 'U';
            continue;
        }
        const Signal& s = store.signal(id);
        if (s.kind() != SignalKind::Logic)
            continue;
        const std::size_t idx = s.holdIndex(t);
        if (m.bit >= 0) {
            if (idx == kNoIndex || static_cast<std::uint32_t>(m.bit) >= s.width())
                out += 'U';
            else
                out += toChar(s.bit(idx, static_cast<std::uint32_t>(m.bit)));
            continue;
        }
        if (idx == kNoIndex)
            out.append(s.width(), 'U');
        else
            out += s.bits(idx);
    }
    return out;
}

} // namespace lvd::wave
