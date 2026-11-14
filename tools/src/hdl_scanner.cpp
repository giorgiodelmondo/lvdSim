// SPDX-License-Identifier: GPL-3.0-or-later
#include "hdl_scanner.hpp"

#include "text_util.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace lvd::tools::scan {

bool isPrimary(UnitKind k) {
    return k != UnitKind::Architecture && k != UnitKind::PackageBody;
}

namespace {

struct Token {
    enum Kind { Ident, Symbol, Literal, Directive } kind = Ident;
    std::string
        text; ///< identifiers lower case; a directive without the backtick; a string literal's contents
    int line = 1;
};

bool identStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool identChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '$';
}

// ---------------------------------------------------------------------------------------------
// VHDL

std::vector<Token> tokenizeVhdl(std::string_view s) {
    std::vector<Token> out;
    int line = 1;
    std::size_t i = 0;
    auto prevAllowsTick = [&] {
        // Right after a name or a closing bracket a ' starts an attribute or a qualified expression
        // (s'event, t'('1')), not a character literal ("when '1'" has a space before it).
        if (i == 0)
            return false;
        const char p = s[i - 1];
        return std::isalnum(static_cast<unsigned char>(p)) != 0 || p == '_' || p == ')' || p == ']';
    };
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\n') {
            ++line;
            ++i;
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
        } else if (c == '-' && i + 1 < s.size() && s[i + 1] == '-') {
            while (i < s.size() && s[i] != '\n')
                ++i;
        } else if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
            i += 2;
            while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) {
                if (s[i] == '\n')
                    ++line;
                ++i;
            }
            i += 2;
        } else if (c == '"') {
            Token t{Token::Literal, {}, line};
            for (++i; i < s.size(); ++i) {
                if (s[i] == '"' && i + 1 < s.size() && s[i + 1] == '"') {
                    t.text += '"';
                    ++i;
                } else if (s[i] == '"' || s[i] == '\n') {
                    break;
                } else {
                    t.text += s[i];
                }
            }
            ++i;
            out.push_back(std::move(t));
        } else if (c == '\'' && !prevAllowsTick() && i + 2 < s.size() && s[i + 2] == '\'') {
            out.push_back({Token::Literal, std::string(1, s[i + 1]), line});
            i += 3;
        } else if (c == '\\') { // extended identifier: kept as written, without the backslashes
            Token t{Token::Ident, {}, line};
            for (++i; i < s.size() && s[i] != '\\' && s[i] != '\n'; ++i)
                t.text += s[i];
            ++i;
            out.push_back(std::move(t));
        } else if (identStart(c)) {
            Token t{Token::Ident, {}, line};
            while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) != 0 || s[i] == '_'))
                t.text += static_cast<char>(std::tolower(static_cast<unsigned char>(s[i++])));
            out.push_back(std::move(t));
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            // Numbers, including based literals 16#FF# and exponents.
            Token t{Token::Literal, {}, line};
            while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) != 0 || s[i] == '_' ||
                                    s[i] == '#' || s[i] == '.'))
                t.text += s[i++];
            out.push_back(std::move(t));
        } else {
            out.push_back({Token::Symbol, std::string(1, c), line});
            ++i;
        }
    }
    return out;
}

bool isIdent(const std::vector<Token>& t, std::size_t i, std::string_view text = {}) {
    return i < t.size() && t[i].kind == Token::Ident && (text.empty() || t[i].text == text);
}

bool isSymbol(const std::vector<Token>& t, std::size_t i, std::string_view text) {
    return i < t.size() && t[i].kind == Token::Symbol && t[i].text == text;
}

void addRef(ScanResult& r, std::string lib, std::string name, int line) {
    for (const UnitRef& x : r.refs)
        if (x.library == lib && x.name == name)
            return;
    r.refs.push_back({std::move(lib), std::move(name), line});
}

} // namespace

ScanResult scanVhdl(std::string_view text) {
    const std::vector<Token> t = tokenizeVhdl(text);
    ScanResult r;
    std::set<std::string> libraries = {"work"};
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (t[i].kind != Token::Ident)
            continue;
        const std::string& w = t[i].text;
        const bool statementStart =
            i == 0 || isSymbol(t, i - 1, ";") ||
            (i >= 1 && t[i - 1].kind == Token::Ident && (t[i - 1].text == "begin" || t[i - 1].text == "is"));

        if (w == "library" && statementStart) {
            for (std::size_t k = i + 1; k < t.size() && !isSymbol(t, k, ";"); ++k)
                if (t[k].kind == Token::Ident)
                    libraries.insert(t[k].text);
            continue;
        }
        if (statementStart && w == "entity" && isIdent(t, i + 1) && isIdent(t, i + 2, "is")) {
            r.units.push_back({UnitKind::Entity, t[i + 1].text, {}, t[i].line});
        } else if (statementStart && w == "architecture" && isIdent(t, i + 1) && isIdent(t, i + 2, "of") &&
                   isIdent(t, i + 3)) {
            r.units.push_back({UnitKind::Architecture, t[i + 1].text, t[i + 3].text, t[i].line});
        } else if (statementStart && w == "package" && isIdent(t, i + 1, "body") && isIdent(t, i + 2)) {
            r.units.push_back({UnitKind::PackageBody, t[i + 2].text, t[i + 2].text, t[i].line});
        } else if (statementStart && w == "package" && isIdent(t, i + 1) && isIdent(t, i + 2, "is")) {
            // Also `package p is new lib.gp generic map (...)`: the generic package is found by the
            // library.name rule below.
            r.units.push_back({UnitKind::Package, t[i + 1].text, {}, t[i].line});
        } else if (statementStart && w == "configuration" && isIdent(t, i + 1) && isIdent(t, i + 2, "of") &&
                   isIdent(t, i + 3)) {
            r.units.push_back({UnitKind::Configuration, t[i + 1].text, t[i + 3].text, t[i].line});
        } else if (statementStart && w == "context" && isIdent(t, i + 1) && isIdent(t, i + 2, "is")) {
            r.units.push_back({UnitKind::Context, t[i + 1].text, {}, t[i].line});
        }

        // library.unit anywhere (use clauses, context references, entity instantiation, selected names).
        if (libraries.count(w) != 0 && isSymbol(t, i + 1, ".") && isIdent(t, i + 2) &&
            (i == 0 || !isSymbol(t, i - 1, "."))) {
            addRef(r, w, t[i + 2].text, t[i].line);
            continue;
        }
        // Component instantiation: label : [component] name (generic|port) map
        if (isSymbol(t, i + 1, ":") && i + 2 < t.size()) {
            std::size_t k = i + 2;
            if (isIdent(t, k, "component"))
                ++k;
            if (isIdent(t, k) && (isIdent(t, k + 1, "generic") || isIdent(t, k + 1, "port")) &&
                isIdent(t, k + 2, "map") && t[k].text != "entity" && t[k].text != "configuration")
                addRef(r, {}, t[k].text, t[k].line);
        }
    }
    // A secondary unit always depends on its primary unit in the same library.
    for (const UnitDecl& u : r.units)
        if (u.kind == UnitKind::Architecture || u.kind == UnitKind::PackageBody ||
            u.kind == UnitKind::Configuration)
            addRef(r, "work", u.primary, u.line);
    // Units the file declares itself are no dependency.
    std::erase_if(r.refs, [&](const UnitRef& ref) {
        return std::any_of(r.units.begin(), r.units.end(), [&](const UnitDecl& u) {
            return isPrimary(u.kind) && u.name == ref.name && (ref.library.empty() || ref.library == "work");
        });
    });
    return r;
}

// ---------------------------------------------------------------------------------------------
// Verilog

namespace {

std::vector<Token> tokenizeVerilog(std::string_view s) {
    std::vector<Token> out;
    int line = 1;
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\n') {
            ++line;
            ++i;
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
        } else if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            while (i < s.size() && s[i] != '\n')
                ++i;
        } else if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
            i += 2;
            while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) {
                if (s[i] == '\n')
                    ++line;
                ++i;
            }
            i += 2;
        } else if (c == '"') {
            Token t{Token::Literal, {}, line};
            for (++i; i < s.size() && s[i] != '"' && s[i] != '\n'; ++i) {
                if (s[i] == '\\' && i + 1 < s.size())
                    ++i;
                t.text += s[i];
            }
            ++i;
            out.push_back(std::move(t));
        } else if (c == '`') {
            Token t{Token::Directive, {}, line};
            for (++i; i < s.size() && identChar(s[i]); ++i)
                t.text += s[i];
            if (t.text == "define") { // the macro body is not code of this file
                while (i < s.size() && s[i] != '\n') {
                    if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == '\n') {
                        ++line;
                        ++i;
                    }
                    ++i;
                }
            }
            out.push_back(std::move(t));
        } else if (c == '\\') { // escaped identifier up to white space
            Token t{Token::Ident, {}, line};
            for (++i; i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])); ++i)
                t.text += s[i];
            out.push_back(std::move(t));
        } else if (identStart(c)) {
            Token t{Token::Ident, {}, line};
            while (i < s.size() && identChar(s[i]))
                t.text += static_cast<char>(std::tolower(static_cast<unsigned char>(s[i++])));
            out.push_back(std::move(t));
        } else if (std::isdigit(static_cast<unsigned char>(c)) || c == '\'') {
            Token t{Token::Literal, {}, line};
            t.text += s[i++];
            while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) != 0 || s[i] == '_' ||
                                    s[i] == '\'' || s[i] == '.' || s[i] == '?'))
                t.text += s[i++];
            out.push_back(std::move(t));
        } else if (c == ':' && i + 1 < s.size() && s[i + 1] == ':') {
            out.push_back({Token::Symbol, "::", line});
            i += 2;
        } else {
            out.push_back({Token::Symbol, std::string(1, c), line});
            ++i;
        }
    }
    return out;
}

const std::set<std::string>& verilogKeywords() {
    static const std::set<std::string> k = {
        "always",    "always_comb",  "always_ff",  "always_latch", "and",        "assert",      "assign",
        "assume",    "automatic",    "begin",      "bit",          "buf",        "bufif0",      "bufif1",
        "byte",      "case",         "casex",      "casez",        "chandle",    "class",       "clocking",
        "cmos",      "const",        "constraint", "cover",        "covergroup", "default",     "defparam",
        "disable",   "do",           "else",       "end",          "endcase",    "endfunction", "endgenerate",
        "endmodule", "endtask",      "enum",       "event",        "export",     "extern",      "final",
        "for",       "force",        "foreach",    "forever",      "fork",       "function",    "generate",
        "genvar",    "if",           "import",     "initial",      "inout",      "input",       "int",
        "integer",   "interconnect", "join",       "join_any",     "join_none",  "local",       "localparam",
        "logic",     "longint",      "modport",    "nand",         "negedge",    "new",         "nmos",
        "nor",       "not",          "notif0",     "notif1",       "or",         "output",      "packed",
        "parameter", "pmos",         "posedge",    "property",     "protected",  "pull0",       "pull1",
        "pulldown",  "pullup",       "rand",       "randc",        "rcmos",      "real",        "realtime",
        "ref",       "reg",          "release",    "repeat",       "return",     "rnmos",       "rpmos",
        "rtran",     "rtranif0",     "rtranif1",   "sequence",     "shortint",   "shortreal",   "signed",
        "specify",   "static",       "string",     "struct",       "supply0",    "supply1",     "task",
        "time",      "tran",         "tranif0",    "tranif1",      "tri",        "tri0",        "tri1",
        "triand",    "trior",        "trireg",     "type",         "typedef",    "union",       "unique",
        "unsigned",  "uwire",        "var",        "virtual",      "void",       "wait",        "wand",
        "while",     "wire",         "wor",        "xnor",         "xor"};
    return k;
}

// Skips a balanced (...) group starting at t[i] == "("; returns the index after it.
std::size_t skipParens(const std::vector<Token>& t, std::size_t i) {
    int depth = 0;
    for (; i < t.size(); ++i) {
        if (isSymbol(t, i, "("))
            ++depth;
        else if (isSymbol(t, i, ")") && --depth == 0)
            return i + 1;
    }
    return i;
}

} // namespace

ScanResult scanVerilog(std::string_view text) {
    const std::vector<Token> t = tokenizeVerilog(text);
    ScanResult r;
    const auto& keywords = verilogKeywords();
    int depth = 0; // inside a module-like unit
    for (std::size_t i = 0; i < t.size(); ++i) {
        const Token& tok = t[i];
        if (tok.kind == Token::Directive) {
            if (tok.text == "include" && i + 1 < t.size() && t[i + 1].kind == Token::Literal)
                r.includes.push_back(t[i + 1].text);
            continue;
        }
        if (tok.kind != Token::Ident)
            continue;
        const std::string& w = tok.text;
        if ((w == "module" || w == "macromodule" || w == "interface" || w == "program" || w == "primitive" ||
             w == "package") &&
            depth == 0) {
            std::size_t k = i + 1;
            if (isIdent(t, k, "automatic") || isIdent(t, k, "static"))
                ++k;
            if (isIdent(t, k)) {
                r.units.push_back(
                    {w == "package" ? UnitKind::VerilogPackage : UnitKind::Module, t[k].text, {}, tok.line});
                ++depth;
                i = k;
            }
            continue;
        }
        if (w == "endmodule" || w == "endinterface" || w == "endprogram" || w == "endprimitive" ||
            w == "endpackage") {
            depth = std::max(0, depth - 1);
            continue;
        }
        // pkg::name and import pkg::*
        if (isSymbol(t, i + 1, "::") && (i == 0 || !isSymbol(t, i - 1, "::")) && w != "std" && w != "$unit") {
            addRef(r, {}, w, tok.line);
            continue;
        }
        if (depth == 0 || keywords.count(w) != 0 || w.front() == '$')
            continue;
        // Instance: TYPE [#(...)] NAME [range] ( ... at the start of a statement.
        const bool start = i == 0 || isSymbol(t, i - 1, ";") || isIdent(t, i - 1, "begin") ||
                           isIdent(t, i - 1, "generate") || isIdent(t, i - 1, "else") ||
                           isSymbol(t, i - 1, ")") || isIdent(t, i - 1, "end");
        if (!start)
            continue;
        std::size_t k = i + 1;
        if (isSymbol(t, k, "#")) {
            ++k;
            if (isSymbol(t, k, "("))
                k = skipParens(t, k);
            else
                ++k; // #5 delay of a primitive
        }
        if (isIdent(t, k) && keywords.count(t[k].text) == 0 &&
            (isSymbol(t, k + 1, "(") || isSymbol(t, k + 1, "[")))
            addRef(r, {}, w, tok.line);
    }
    std::erase_if(r.refs, [&](const UnitRef& ref) {
        return std::any_of(r.units.begin(), r.units.end(),
                           [&](const UnitDecl& u) { return u.name == ref.name; });
    });
    return r;
}

} // namespace lvd::tools::scan
