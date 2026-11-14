// SPDX-License-Identifier: GPL-3.0-or-later
#include "coverage.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <unordered_map>

namespace lvd::tools::cov {

namespace {

int idx(Kind k) {
    return static_cast<int>(k);
}

char lowerChar(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool equalNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lowerChar(a[i]) != lowerChar(b[i]))
            return false;
    return true;
}

// ---- a small XML reader, enough for nvc's export: elements, attributes, comments, declarations
// ----------------

struct Attr {
    std::string_view name;
    std::string value;
};

struct Tag {
    enum class Type { Open, Close, Empty } type = Type::Open;
    std::string_view name;
    std::vector<Attr> attrs;
    const std::string* attr(std::string_view n) const {
        for (const Attr& a : attrs)
            if (a.name == n)
                return &a.value;
        return nullptr;
    }
};

std::string decodeEntities(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out += s[i];
            continue;
        }
        const std::size_t semi = s.find(';', i);
        if (semi == std::string_view::npos || semi - i > 10) {
            out += s[i]; // a bare '&' (nvc does not escape names): keep it
            continue;
        }
        const std::string_view ent = s.substr(i + 1, semi - i - 1);
        unsigned code = 0;
        bool known = true;
        if (ent == "lt")
            code = '<';
        else if (ent == "gt")
            code = '>';
        else if (ent == "amp")
            code = '&';
        else if (ent == "quot")
            code = '"';
        else if (ent == "apos")
            code = '\'';
        else if (ent.size() > 1 && ent[0] == '#') {
            const bool hex = ent[1] == 'x' || ent[1] == 'X';
            const std::string_view digits = ent.substr(hex ? 2 : 1);
            auto [p, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), code, hex ? 16 : 10);
            known = ec == std::errc() && p == digits.data() + digits.size() && code > 0 && code < 0x80;
        } else {
            known = false;
        }
        if (!known) {
            out += s[i];
            continue;
        }
        out += static_cast<char>(code);
        i = semi;
    }
    return out;
}

class XmlReader {
public:
    explicit XmlReader(std::string_view text)
        : s_(text) {}

    /// The next tag; false at the end of the input or on an error (error() says which).
    bool next(Tag& tag) {
        while (true) {
            const std::size_t lt = s_.find('<', pos_);
            if (lt == std::string_view::npos) {
                pos_ = s_.size();
                return false;
            }
            pos_ = lt;
            if (s_.compare(pos_, 4, "<!--") == 0) {
                const std::size_t end = s_.find("-->", pos_ + 4);
                if (end == std::string_view::npos)
                    return fail("unterminated comment");
                pos_ = end + 3;
                continue;
            }
            if (s_.compare(pos_, 2, "<?") == 0 || s_.compare(pos_, 2, "<!") == 0) {
                const std::size_t end = s_.find('>', pos_ + 2);
                if (end == std::string_view::npos)
                    return fail("unterminated declaration");
                pos_ = end + 1;
                continue;
            }
            return readTag(tag);
        }
    }
    const std::string& error() const noexcept { return error_; }
    std::size_t offset() const noexcept { return pos_; }

private:
    bool fail(const char* what) {
        error_ = std::string(what) + " at offset " + std::to_string(pos_);
        return false;
    }
    static bool isNameChar(char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-' || c == ':' ||
               c == '.';
    }
    void skipSpace() {
        while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_])) != 0)
            ++pos_;
    }
    std::string_view readName() {
        const std::size_t start = pos_;
        while (pos_ < s_.size() && isNameChar(s_[pos_]))
            ++pos_;
        return s_.substr(start, pos_ - start);
    }
    bool readTag(Tag& tag) {
        tag = Tag{};
        ++pos_; // '<'
        if (pos_ < s_.size() && s_[pos_] == '/') {
            ++pos_;
            tag.type = Tag::Type::Close;
            tag.name = readName();
            skipSpace();
            if (pos_ >= s_.size() || s_[pos_] != '>')
                return fail("bad closing tag");
            ++pos_;
            return true;
        }
        tag.name = readName();
        if (tag.name.empty())
            return fail("bad tag");
        while (true) {
            skipSpace();
            if (pos_ >= s_.size())
                return fail("unterminated tag");
            if (s_[pos_] == '>') {
                ++pos_;
                return true;
            }
            if (s_.compare(pos_, 2, "/>") == 0) {
                pos_ += 2;
                tag.type = Tag::Type::Empty;
                return true;
            }
            Attr a;
            a.name = readName();
            if (a.name.empty())
                return fail("bad attribute");
            skipSpace();
            if (pos_ >= s_.size() || s_[pos_] != '=')
                return fail("attribute without value");
            ++pos_;
            skipSpace();
            if (pos_ >= s_.size() || (s_[pos_] != '"' && s_[pos_] != '\''))
                return fail("attribute value without quotes");
            const char q = s_[pos_++];
            const std::size_t end = s_.find(q, pos_);
            if (end == std::string_view::npos)
                return fail("unterminated attribute value");
            a.value = decodeEntities(s_.substr(pos_, end - pos_));
            pos_ = end + 1;
            tag.attrs.push_back(std::move(a));
        }
    }

    std::string_view s_;
    std::size_t pos_ = 0;
    std::string error_;
};

bool kindOfElement(std::string_view name, Kind& k) {
    for (Kind c : kAllKinds)
        if (name == kindElement(c)) {
            k = c;
            return true;
        }
    return false;
}

bool parseInt(const std::string& s, std::int64_t& v) {
    const char* b = s.data();
    const char* e = b + s.size();
    auto [p, ec] = std::from_chars(b, e, v);
    return ec == std::errc() && p == e;
}

constexpr std::uint32_t kMaskExcludeUnreachable = 1u << 11; // COVER_MASK_EXCLUDE_UNREACHABLE of nvc

int collectBinsInto(const Database& db, int scope, int instance, bool recursive, std::vector<int>& out) {
    const Scope& s = db.scopes[static_cast<std::size_t>(scope)];
    int n = 0;
    for (int b : s.bins) {
        out.push_back(b);
        ++n;
    }
    for (int c : s.children) {
        const Scope& cs = db.scopes[static_cast<std::size_t>(c)];
        if (cs.isInstance() && c != instance && !recursive)
            continue;
        n += collectBinsInto(db, c, instance, recursive, out);
    }
    return n;
}

} // namespace

const char* kindLabel(Kind k) {
    switch (k) {
    case Kind::Statement:
        return "Stmt";
    case Kind::Branch:
        return "Branch";
    case Kind::Expression:
        return "Expr";
    case Kind::Toggle:
        return "Toggle";
    case Kind::State:
        return "FSM";
    case Kind::Functional:
        return "Func";
    }
    return "?";
}

const char* kindElement(Kind k) {
    switch (k) {
    case Kind::Statement:
        return "statement";
    case Kind::Branch:
        return "branch";
    case Kind::Expression:
        return "expression";
    case Kind::Toggle:
        return "toggle";
    case Kind::State:
        return "state";
    case Kind::Functional:
        return "functional";
    }
    return "?";
}

const char* kindOption(Kind k) {
    switch (k) {
    case Kind::State:
        return "fsm-state";
    default:
        return kindElement(k);
    }
}

// ---- Counts
// ------------------------------------------------------------------------------------------------------

void Counts::add(const Bin& b) {
    ++total[idx(b.kind)];
    if (b.covered())
        ++hit[idx(b.kind)];
}

void Counts::add(const Counts& o) {
    for (int i = 0; i < kKindCount; ++i) {
        total[i] += o.total[i];
        hit[i] += o.hit[i];
    }
}

double Counts::percent(Kind k) const noexcept {
    const std::int64_t t = total[idx(k)];
    return t > 0 ? 100.0 * static_cast<double>(hit[idx(k)]) / static_cast<double>(t) : -1.0;
}

std::int64_t Counts::allTotal() const noexcept {
    std::int64_t n = 0;
    for (std::int64_t t : total)
        n += t;
    return n;
}

std::int64_t Counts::allHit() const noexcept {
    std::int64_t n = 0;
    for (std::int64_t h : hit)
        n += h;
    return n;
}

double Counts::overall() const noexcept {
    const std::int64_t t = allTotal();
    return t > 0 ? 100.0 * static_cast<double>(allHit()) / static_cast<double>(t) : -1.0;
}

// ---- Database
// ----------------------------------------------------------------------------------------------------

std::vector<int> Database::topInstances() const {
    return scopes.empty() ? std::vector<int>{} : childInstances(0);
}

std::vector<int> Database::childInstances(int instance) const {
    std::vector<int> out;
    if (instance < 0 || static_cast<std::size_t>(instance) >= scopes.size())
        return out;
    // Breadth first through the scopes that are not instances.
    std::vector<int> work = scopes[static_cast<std::size_t>(instance)].children;
    for (std::size_t i = 0; i < work.size(); ++i) {
        const Scope& s = scopes[static_cast<std::size_t>(work[i])];
        if (s.isInstance())
            out.push_back(work[i]);
        else
            // push_back instead of insert(range): GCC 13/14 at -O2 report a false
            // -Wstringop-overflow for the range insert here.
            for (int child : s.children)
                work.push_back(child);
    }
    return out;
}

std::vector<int> Database::instanceBins(int instance) const {
    std::vector<int> out;
    if (instance >= 0 && static_cast<std::size_t>(instance) < scopes.size())
        collectBinsInto(*this, instance, instance, false, out);
    return out;
}

Counts Database::counts(int instance, bool recursive) const {
    Counts c;
    if (instance < 0 || static_cast<std::size_t>(instance) >= scopes.size())
        return c;
    std::vector<int> b;
    collectBinsInto(*this, instance, instance, recursive, b);
    for (int i : b)
        c.add(bins[static_cast<std::size_t>(i)]);
    return c;
}

Counts Database::totals() const {
    Counts c;
    for (const Bin& b : bins)
        c.add(b);
    return c;
}

int Database::findScope(std::string_view path) const {
    // Split into words on '.' or '/'; '(' starts an index that belongs to the word (a generate index).
    std::vector<std::string_view> words;
    std::size_t start = 0;
    int depth = 0;
    for (std::size_t i = 0; i <= path.size(); ++i) {
        const char c = i < path.size() ? path[i] : '/';
        if (c == '(')
            ++depth;
        else if (c == ')')
            --depth;
        if (depth == 0 && (c == '.' || c == '/')) {
            if (i > start)
                words.push_back(path.substr(start, i - start));
            start = i + 1;
        }
    }
    if (words.empty() || scopes.empty())
        return words.empty() && !scopes.empty() ? 0 : -1;
    std::size_t w = 0;
    if (equalNoCase(words[0], scopes[0].name))
        ++w;
    int cur = 0;
    for (; w < words.size(); ++w) {
        int found = -1;
        for (int c : scopes[static_cast<std::size_t>(cur)].children)
            if (equalNoCase(scopes[static_cast<std::size_t>(c)].name, words[w])) {
                found = c;
                break;
            }
        if (found < 0)
            return -1;
        cur = found;
    }
    return cur;
}

int Database::mainFile(int instance) const {
    std::unordered_map<int, int> n;
    for (int b : instanceBins(instance)) {
        const int f = bins[static_cast<std::size_t>(b)].file;
        if (f >= 0)
            ++n[f];
    }
    int best = -1, bestCount = 0;
    for (const auto& [f, count] : n)
        if (count > bestCount || (count == bestCount && f < best)) {
            best = f;
            bestCount = count;
        }
    return best;
}

// ---- per line, toggles, states
// -----------------------------------------------------------------------------------

std::map<int, LineCoverage> lineCoverage(const Database& db, int instance, int file, bool recursive) {
    std::map<int, LineCoverage> out;
    if (instance < 0 || static_cast<std::size_t>(instance) >= db.scopes.size())
        return out;
    std::vector<int> bins;
    collectBinsInto(db, instance, instance, recursive, bins);
    for (int i : bins) {
        const Bin& b = db.bins[static_cast<std::size_t>(i)];
        if (b.file != file || b.line <= 0)
            continue;
        LineCoverage& l = out[b.line];
        ++l.bins;
        if (b.covered())
            ++l.hitBins;
        else
            ++l.missed[idx(b.kind)];
        const std::int64_t h = b.unreachable ? 0 : b.hits;
        if (b.kind == Kind::Statement) {
            l.hits = l.hasStatement ? std::max(l.hits, h) : h;
            l.hasStatement = true;
        } else if (!l.hasStatement) {
            l.hits = std::max(l.hits, h);
        }
    }
    return out;
}

int ToggleSignal::coveredBits() const {
    return static_cast<int>(
        std::count_if(bits.begin(), bits.end(), [](const Bit& b) { return b.covered(); }));
}

std::vector<ToggleSignal> toggleSignals(const Database& db, int instance) {
    std::vector<ToggleSignal> out;
    std::unordered_map<int, std::size_t> bySignal;
    // (signal, index) -> position in its bits: the two bins of a bit are usually next to each other, but a
    // wide bus must not cost a search per bin.
    std::unordered_map<std::string, std::size_t> bitPos;
    for (int i : db.instanceBins(instance)) {
        const Bin& b = db.bins[static_cast<std::size_t>(i)];
        if (b.kind != Kind::Toggle)
            continue;
        auto it = bySignal.find(b.scope);
        if (it == bySignal.end()) {
            const Scope& s = db.scopes[static_cast<std::size_t>(b.scope)];
            ToggleSignal t;
            t.name = s.name;
            t.scope = b.scope;
            t.line = s.line;
            t.file = s.file;
            it = bySignal.emplace(b.scope, out.size()).first;
            out.push_back(std::move(t));
        }
        ToggleSignal& t = out[it->second];
        // label: "(3).BIN_0_TO_1", "BIN_1_TO_0", ".F(1).BIN_0_TO_1"
        std::string_view label = b.label;
        std::string_view index;
        std::string_view which = label;
        if (const std::size_t p = label.rfind("BIN_"); p != std::string_view::npos) {
            index = label.substr(0, p);
            which = label.substr(p);
            if (!index.empty() && index.back() == '.')
                index.remove_suffix(1);
        }
        // The bin label lost the dot before a record field ("A.BIN_0_TO_1" of R.A): the index keeps it, so
        // that name + index reads R.A.
        std::string indexText(index);
        if (!indexText.empty() && indexText.front() != '(' && indexText.front() != '.')
            indexText.insert(indexText.begin(), '.');
        const std::string key = std::to_string(b.scope) + '\x01' + indexText;
        auto [pos, added] = bitPos.try_emplace(key, t.bits.size());
        if (added) {
            t.bits.push_back({});
            t.bits.back().index = indexText;
            t.bits.back().excluded = b.excluded;
        }
        ToggleSignal::Bit* bit = &t.bits[pos->second];
        bit->excluded = bit->excluded && b.excluded;
        if (b.unreachable)
            bit->unreachable = true;
        const std::int64_t h = b.unreachable ? 0 : b.hits;
        if (which == "BIN_0_TO_1") {
            bit->rise += h;
            bit->hasRise = true;
            bit->riseExcluded = bit->riseExcluded || b.excluded;
        } else if (which == "BIN_1_TO_0") {
            bit->fall += h;
            bit->hasFall = true;
            bit->fallExcluded = bit->fallExcluded || b.excluded;
        }
    }
    return out;
}

std::vector<StateVariable> stateVariables(const Database& db, int instance) {
    std::vector<StateVariable> out;
    std::unordered_map<int, std::size_t> byScope;
    for (int i : db.instanceBins(instance)) {
        const Bin& b = db.bins[static_cast<std::size_t>(i)];
        if (b.kind != Kind::State)
            continue;
        auto it = byScope.find(b.scope);
        if (it == byScope.end()) {
            const Scope& s = db.scopes[static_cast<std::size_t>(b.scope)];
            StateVariable v;
            v.name = s.name;
            v.scope = b.scope;
            v.line = s.line;
            v.file = s.file;
            it = byScope.emplace(b.scope, out.size()).first;
            out.push_back(std::move(v));
        }
        std::string state = b.label;
        if (const std::size_t p = state.find("BIN_STATE."); p != std::string::npos)
            state = state.substr(p + 10);
        out[it->second].states.emplace_back(std::move(state), b.hits);
    }
    return out;
}

// ---- parsing
// -----------------------------------------------------------------------------------------------------

ParseResult parseExportXml(std::string_view xml) {
    ParseResult r;
    Database& db = r.db;
    XmlReader reader(xml);
    Tag tag;
    std::vector<int> stack; // open scopes
    bool sawRoot = false, closedRoot = false;
    std::unordered_map<std::string, int> fileIndex;

    auto fileOf = [&](const std::string& f) {
        const auto it = fileIndex.find(f);
        if (it != fileIndex.end())
            return it->second;
        const int n = static_cast<int>(db.files.size());
        db.files.push_back(f);
        fileIndex.emplace(f, n);
        return n;
    };

    while (reader.next(tag)) {
        if (closedRoot) {
            r.error = "content after the top-level scope";
            return r;
        }
        if (tag.name == "scope") {
            if (tag.type == Tag::Type::Close) {
                if (stack.empty()) {
                    r.error = "unbalanced </scope>";
                    return r;
                }
                stack.pop_back();
                if (stack.empty())
                    closedRoot = true;
                continue;
            }
            Scope s;
            if (const std::string* n = tag.attr("name"))
                s.name = *n;
            if (const std::string* b = tag.attr("block_name"))
                s.blockName = *b;
            const int self = static_cast<int>(db.scopes.size());
            if (!stack.empty()) {
                const Scope& p = db.scopes[static_cast<std::size_t>(stack.back())];
                s.parent = stack.back();
                s.path = p.path + "." + s.name;
                s.file = p.file;
                s.line = p.line;
                s.instance = p.instance;
            } else {
                if (sawRoot) {
                    r.error = "more than one top-level scope";
                    return r;
                }
                sawRoot = true;
                s.path = s.name;
                db.library = s.name;
            }
            if (s.isInstance())
                s.instance = self;
            if (const std::string* f = tag.attr("file"))
                s.file = fileOf(*f);
            if (const std::string* l = tag.attr("line")) {
                std::int64_t v = 0;
                if (parseInt(*l, v) && v > 0 && v < (1LL << 31))
                    s.line = static_cast<int>(v);
            }
            if (s.parent >= 0)
                db.scopes[static_cast<std::size_t>(s.parent)].children.push_back(self);
            db.scopes.push_back(std::move(s));
            if (tag.type == Tag::Type::Open)
                stack.push_back(self);
            else if (stack.empty())
                closedRoot = true;
            continue;
        }
        Kind kind{};
        if (kindOfElement(tag.name, kind)) {
            if (stack.empty()) {
                r.error = std::string("<") + std::string(tag.name) + "> outside a scope";
                return r;
            }
            const std::string* hier = tag.attr("hier");
            const std::string* data = tag.attr("data");
            std::int64_t v = 0;
            if (hier == nullptr || data == nullptr || !parseInt(*data, v)) {
                r.error =
                    std::string("<") + std::string(tag.name) + "> without hier or a numeric data attribute";
                return r;
            }
            const int scope = stack.back();
            Scope& s = db.scopes[static_cast<std::size_t>(scope)];
            Bin b;
            b.kind = kind;
            b.hier = *hier;
            // nvc prints the 32 bit counter as signed: the top bit marks a toggle that cannot happen.
            if (v < 0) {
                b.unreachable = true;
                b.hits = 0;
            } else {
                b.hits = v;
            }
            if (b.hier.size() > s.path.size() &&
                equalNoCase(std::string_view(b.hier).substr(0, s.path.size()), s.path))
                b.label = b.hier.substr(s.path.size() + (b.hier[s.path.size()] == '.' ? 1 : 0));
            else if (const std::size_t p = b.hier.rfind("BIN_"); p != std::string::npos)
                b.label = b.hier.substr(p);
            b.file = s.file;
            b.line = s.line;
            b.scope = scope;
            s.bins.push_back(static_cast<int>(db.bins.size()));
            db.bins.push_back(std::move(b));
            continue;
        }
        // Anything else (a future attribute element, for example) is skipped with its content.
    }
    if (!reader.error().empty()) {
        r.error = reader.error();
        return r;
    }
    if (!sawRoot) {
        r.error = "not an nvc coverage export (no <scope> element)";
        return r;
    }
    if (!stack.empty()) {
        r.error = "the export ends before its last </scope>";
        return r;
    }
    return r;
}

namespace {

/// A JSON value, as much of JSON as the export uses (all of it, numbers as 64 bit integers or doubles).
struct JsonValue {
    enum class Type { Null, Bool, Int, Double, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    std::int64_t integer = 0;
    double real = 0.0;
    std::string text;
    std::vector<JsonValue> items;
    std::vector<std::pair<std::string, JsonValue>> members;

    const JsonValue* get(std::string_view key) const {
        for (const auto& [k, v] : members)
            if (k == key)
                return &v;
        return nullptr;
    }
    bool isInt() const noexcept { return type == Type::Int; }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view s)
        : s_(s) {}

    bool parseDocument(JsonValue& v) {
        if (!value(v, 0))
            return false;
        skipSpace();
        if (pos_ != s_.size())
            return fail("text after the JSON value");
        return true;
    }
    const std::string& error() const noexcept { return error_; }

private:
    bool fail(const std::string& what) {
        if (error_.empty())
            error_ = what + " at offset " + std::to_string(pos_);
        return false;
    }
    void skipSpace() {
        while (pos_ < s_.size() &&
               (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r'))
            ++pos_;
    }
    bool literal(std::string_view word) {
        if (s_.substr(pos_, word.size()) != word)
            return fail("unexpected character");
        pos_ += word.size();
        return true;
    }
    static void appendUtf8(std::string& out, unsigned cp) {
        // nvc names are ISO-8859-1 and escaped as \u00XX: those stay single bytes, as in the XML export
        if (cp < 0x100) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    bool string(std::string& out) {
        ++pos_; // opening quote
        while (pos_ < s_.size()) {
            const char c = s_[pos_++];
            if (c == '"')
                return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= s_.size())
                break;
            const char e = s_[pos_++];
            switch (e) {
            case '"':
            case '\\':
            case '/':
                out += e;
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'u': {
                if (pos_ + 4 > s_.size())
                    return fail("short \\u escape");
                unsigned cp = 0;
                const auto [p, ec] = std::from_chars(s_.data() + pos_, s_.data() + pos_ + 4, cp, 16);
                if (ec != std::errc() || p != s_.data() + pos_ + 4)
                    return fail("bad \\u escape");
                pos_ += 4;
                appendUtf8(out, cp);
                break;
            }
            default:
                return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }
    bool number(JsonValue& v) {
        const std::size_t start = pos_;
        if (pos_ < s_.size() && s_[pos_] == '-')
            ++pos_;
        bool isReal = false;
        while (pos_ < s_.size()) {
            const char c = s_[pos_];
            if (c == '.' || c == 'e' || c == 'E' || c == '+' || (c == '-' && pos_ > start))
                isReal = true;
            else if (!std::isdigit(static_cast<unsigned char>(c)))
                break;
            ++pos_;
        }
        const char* b = s_.data() + start;
        const char* e = s_.data() + pos_;
        if (b == e)
            return fail("expected a value");
        if (!isReal) {
            const auto [p, ec] = std::from_chars(b, e, v.integer);
            if (ec == std::errc() && p == e) {
                v.type = JsonValue::Type::Int;
                return true;
            }
        }
        v.real = std::strtod(std::string(b, e).c_str(), nullptr);
        v.type = JsonValue::Type::Double;
        return true;
    }
    bool value(JsonValue& v, int depth) {
        if (depth > 64)
            return fail("nested too deeply");
        skipSpace();
        if (pos_ >= s_.size())
            return fail("unexpected end");
        const char c = s_[pos_];
        if (c == '{') {
            v.type = JsonValue::Type::Object;
            ++pos_;
            skipSpace();
            if (pos_ < s_.size() && s_[pos_] == '}') {
                ++pos_;
                return true;
            }
            for (;;) {
                skipSpace();
                if (pos_ >= s_.size() || s_[pos_] != '"')
                    return fail("expected a member name");
                std::string key;
                if (!string(key))
                    return false;
                skipSpace();
                if (pos_ >= s_.size() || s_[pos_] != ':')
                    return fail("expected ':'");
                ++pos_;
                JsonValue member;
                if (!value(member, depth + 1))
                    return false;
                v.members.emplace_back(std::move(key), std::move(member));
                skipSpace();
                if (pos_ < s_.size() && s_[pos_] == ',') {
                    ++pos_;
                    continue;
                }
                if (pos_ < s_.size() && s_[pos_] == '}') {
                    ++pos_;
                    return true;
                }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            v.type = JsonValue::Type::Array;
            ++pos_;
            skipSpace();
            if (pos_ < s_.size() && s_[pos_] == ']') {
                ++pos_;
                return true;
            }
            for (;;) {
                JsonValue item;
                if (!value(item, depth + 1))
                    return false;
                v.items.push_back(std::move(item));
                skipSpace();
                if (pos_ < s_.size() && s_[pos_] == ',') {
                    ++pos_;
                    continue;
                }
                if (pos_ < s_.size() && s_[pos_] == ']') {
                    ++pos_;
                    return true;
                }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            v.type = JsonValue::Type::String;
            return string(v.text);
        }
        if (c == 't') {
            v.type = JsonValue::Type::Bool;
            v.boolean = true;
            return literal("true");
        }
        if (c == 'f') {
            v.type = JsonValue::Type::Bool;
            return literal("false");
        }
        if (c == 'n')
            return literal("null");
        return number(v);
    }

    std::string_view s_;
    std::size_t pos_ = 0;
    std::string error_;
};

std::int64_t intMember(const JsonValue& o, std::string_view key, std::int64_t def) {
    const JsonValue* v = o.get(key);
    return v != nullptr && v->isInt() ? v->integer : def;
}

bool boolMember(const JsonValue& o, std::string_view key) {
    const JsonValue* v = o.get(key);
    return v != nullptr && v->type == JsonValue::Type::Bool && v->boolean;
}

/// The part of a bin name after the scope path (BIN_TRUE, BIN_0_TO_1 ...), as the XML reader does it.
std::string binLabel(const std::string& hier, const std::string& path) {
    if (hier.size() > path.size() && equalNoCase(std::string_view(hier).substr(0, path.size()), path))
        return hier.substr(path.size() + (hier[path.size()] == '.' ? 1 : 0));
    if (const std::size_t p = hier.rfind("BIN_"); p != std::string::npos)
        return hier.substr(p);
    return {};
}

} // namespace

ParseResult parseExportJson(std::string_view json) {
    ParseResult r;
    Database& db = r.db;
    JsonValue root;
    JsonParser parser(json);
    if (!parser.parseDocument(root)) {
        r.error = "not a valid JSON coverage export: " + parser.error();
        return r;
    }
    if (root.type != JsonValue::Type::Object) {
        r.error = "not an nvc coverage export (not a JSON object)";
        return r;
    }
    if (intMember(root, "v", 0) != 1) {
        r.error = "unknown coverage export version (expected \"v\":1)";
        return r;
    }
    db.mask = static_cast<std::uint32_t>(intMember(root, "mask", 0));

    const JsonValue* files = root.get("files");
    const JsonValue* scopes = root.get("scopes");
    const JsonValue* bins = root.get("bins");
    if (files == nullptr || scopes == nullptr || bins == nullptr || files->type != JsonValue::Type::Array ||
        scopes->type != JsonValue::Type::Array || bins->type != JsonValue::Type::Array) {
        r.error = "coverage export without \"files\", \"scopes\" or \"bins\" arrays";
        return r;
    }
    for (const JsonValue& f : files->items) {
        if (f.type != JsonValue::Type::String) {
            r.error = "a file name is not a string";
            return r;
        }
        db.files.push_back(f.text);
    }
    const auto fileIndex = [&](std::int64_t f) {
        return f >= 0 && f < static_cast<std::int64_t>(db.files.size()) ? static_cast<int>(f) : -1;
    };

    for (const JsonValue& o : scopes->items) {
        const int self = static_cast<int>(db.scopes.size());
        const JsonValue* name = o.get("name");
        const std::int64_t parent = intMember(o, "parent", -2);
        if (o.type != JsonValue::Type::Object || name == nullptr || name->type != JsonValue::Type::String ||
            parent < -1 || parent >= self || (parent == -1) != (self == 0)) {
            r.error = "scope " + std::to_string(self) + " has no name or a wrong parent";
            return r;
        }
        Scope s;
        s.name = name->text;
        if (const JsonValue* b = o.get("block"); b != nullptr && b->type == JsonValue::Type::String)
            s.blockName = b->text;
        if (parent >= 0) {
            const Scope& p = db.scopes[static_cast<std::size_t>(parent)];
            s.parent = static_cast<int>(parent);
            s.path = p.path + "." + s.name;
            s.file = p.file;
            s.line = p.line;
            s.instance = p.instance;
        } else {
            s.path = s.name;
            db.library = s.name;
        }
        if (s.isInstance())
            s.instance = self;
        if (const int f = fileIndex(intMember(o, "file", -1)); f >= 0)
            s.file = f;
        if (const std::int64_t l = intMember(o, "line", 0); l > 0 && l < (1LL << 31))
            s.line = static_cast<int>(l);
        if (s.parent >= 0)
            db.scopes[static_cast<std::size_t>(s.parent)].children.push_back(self);
        db.scopes.push_back(std::move(s));
    }
    if (db.scopes.empty()) {
        r.error = "not an nvc coverage export (no scopes)";
        return r;
    }

    for (const JsonValue& o : bins->items) {
        const std::int64_t scope = intMember(o, "scope", -1);
        const JsonValue* kind = o.get("kind");
        const JsonValue* hier = o.get("hier");
        const JsonValue* hits = o.get("hits");
        Kind k{};
        if (o.type != JsonValue::Type::Object || scope < 0 ||
            scope >= static_cast<std::int64_t>(db.scopes.size()) || kind == nullptr ||
            kind->type != JsonValue::Type::String || hier == nullptr ||
            hier->type != JsonValue::Type::String || hits == nullptr || !hits->isInt()) {
            r.error = "bin " + std::to_string(db.bins.size()) + " has no scope, kind, hier or hits";
            return r;
        }
        if (!kindOfElement(kind->text, k))
            continue; // a kind this reader does not know
        Scope& s = db.scopes[static_cast<std::size_t>(scope)];
        Bin b;
        b.kind = k;
        b.hier = hier->text;
        b.unreachable = boolMember(o, "unreachable");
        b.hits = b.unreachable ? 0 : hits->integer;
        b.label = binLabel(b.hier, s.path);
        const int f = fileIndex(intMember(o, "file", -1));
        b.file = f >= 0 ? f : s.file;
        const std::int64_t line = intMember(o, "line", 0);
        b.line = line > 0 && line < (1LL << 31) ? static_cast<int>(line) : s.line;
        const std::int64_t column = intMember(o, "column", 0);
        b.column = column > 0 && column < (1LL << 31) ? static_cast<int>(column) : 0;
        b.threshold = std::max<std::int64_t>(intMember(o, "threshold", 1), 1);
        b.userExcluded = boolMember(o, "user_excluded");
        // As nvc's report decides it: the exclude flag, or with exclude-unreachable a toggle or expression
        // term that cannot happen.
        b.excluded =
            boolMember(o, "excluded") || ((db.mask & kMaskExcludeUnreachable) != 0 && b.unreachable &&
                                          (k == Kind::Toggle || k == Kind::Expression));
        b.scope = static_cast<int>(scope);
        s.bins.push_back(static_cast<int>(db.bins.size()));
        db.bins.push_back(std::move(b));
    }
    return r;
}

ParseResult parseExport(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
        ++i;
    if (i < text.size() && text[i] == '{')
        return parseExportJson(text);
    return parseExportXml(text);
}

std::string displayName(std::string_view name) {
    if (!name.empty() && name.front() == '\\')
        return std::string(name);
    std::string out(name);
    for (char& c : out)
        c = lowerChar(c);
    return out;
}

std::string displayPath(const Database& db, int scope) {
    std::vector<int> chain;
    for (int s = scope; s > 0; s = db.scopes[static_cast<std::size_t>(s)].parent)
        chain.push_back(s);
    if (chain.empty())
        return "/";
    std::string out;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        out += "/" + displayName(db.scopes[static_cast<std::size_t>(*it)].name);
    return out;
}

// ---- planning
// ----------------------------------------------------------------------------------------------------

std::vector<Kind> parseKinds(std::string_view text) {
    std::vector<Kind> out;
    auto add = [&](Kind k) {
        if (std::find(out.begin(), out.end(), k) == out.end())
            out.push_back(k);
    };
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i < text.size() && text[i] != ',' && text[i] != ' ')
            continue;
        std::string w(text.substr(start, i - start));
        start = i + 1;
        for (char& c : w)
            c = lowerChar(c);
        if (w.empty())
            continue;
        if (w == "all") {
            for (Kind k : kAllKinds)
                add(k);
            continue;
        }
        bool matched = false;
        for (Kind k : kAllKinds)
            if (w == kindOption(k) || w == kindElement(k) || (k == Kind::State && w == "fsm")) {
                add(k);
                matched = true;
            }
        if (matched)
            continue;
        // letters: s statement, b branch, c and e expression, f FSM states, t toggle
        if (w.find_first_not_of("sbceft") == std::string::npos)
            for (char c : w)
                switch (c) {
                case 's':
                    add(Kind::Statement);
                    break;
                case 'b':
                    add(Kind::Branch);
                    break;
                case 'c':
                case 'e':
                    add(Kind::Expression);
                    break;
                case 'f':
                    add(Kind::State);
                    break;
                case 't':
                    add(Kind::Toggle);
                    break;
                default:
                    break;
                }
    }
    std::sort(out.begin(), out.end(), [](Kind a, Kind b) { return idx(a) < idx(b); });
    return out;
}

std::vector<std::string> elaborationOptions(const std::vector<Kind>& kinds, const std::string& databaseFile) {
    std::string list;
    for (Kind k : kinds.empty() ? std::vector<Kind>(kAllKinds.begin(), kAllKinds.end()) : kinds) {
        if (!list.empty())
            list += ',';
        list += kindOption(k);
    }
    std::vector<std::string> out = {"--cover=" + list};
    if (!databaseFile.empty())
        out.push_back("--cover-file=" + databaseFile);
    return out;
}

bool addElaborationOptions(std::vector<std::string>& nvcArgs, const std::vector<Kind>& kinds,
                           const std::string& databaseFile) {
    auto e = std::find(nvcArgs.begin(), nvcArgs.end(), "-e");
    if (e == nvcArgs.end())
        return false;
    const std::ptrdiff_t at = e - nvcArgs.begin() + 1;
    nvcArgs.erase(std::remove_if(nvcArgs.begin() + at, nvcArgs.end(),
                                 [](const std::string& a) {
                                     return a == "--cover" || a.rfind("--cover=", 0) == 0 ||
                                            a.rfind("--cover-file=", 0) == 0;
                                 }),
                  nvcArgs.end());
    const std::vector<std::string> opts = elaborationOptions(kinds, databaseFile);
    nvcArgs.insert(nvcArgs.begin() + at, opts.begin(), opts.end());
    return true;
}

std::vector<std::string> exportArgs(const std::vector<std::string>& databases, const std::string& file,
                                    ExportFormat format) {
    std::vector<std::string> out = {
        "--cover-export", format == ExportFormat::Json ? "--format=json" : "--format=xml", "-o", file};
    out.insert(out.end(), databases.begin(), databases.end());
    return out;
}

std::vector<std::string> mergeArgs(const std::vector<std::string>& databases, const std::string& outFile) {
    std::vector<std::string> out = {"--cover-merge", "-o", outFile};
    out.insert(out.end(), databases.begin(), databases.end());
    return out;
}

std::vector<std::string> reportArgs(const std::vector<std::string>& databases, const std::string& outDir) {
    std::vector<std::string> out = {"--cover-report", "-o", outDir};
    out.insert(out.end(), databases.begin(), databases.end());
    return out;
}

} // namespace lvd::tools::cov
