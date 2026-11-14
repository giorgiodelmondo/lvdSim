// SPDX-License-Identifier: GPL-3.0-or-later
#include <lvdwave/expression.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace lvd::wave {

namespace {

// ---- Bits helpers ----------------------------------------------------------------------------------------

/// '0' '1' 'U' or 'X': the class of a bit for the operators (L and H are 0 and 1, Z W - are unknown).
char norm(char c) noexcept {
    switch (c) {
    case '0':
    case 'L':
        return '0';
    case '1':
    case 'H':
        return '1';
    case 'U':
        return 'U';
    default:
        return 'X';
    }
}

/// The character at MSB-first position i of value `s` extended to width w (zero or sign extension).
char extAt(const std::string& s, std::uint32_t w, std::uint32_t i, bool sgn) noexcept {
    const std::uint32_t off = w - static_cast<std::uint32_t>(s.size());
    if (i < off)
        return sgn && !s.empty() ? s[0] : '0';
    return s[i - off];
}

char andBit(char a, char b) noexcept {
    if (a == '0' || b == '0')
        return '0';
    if (a == '1' && b == '1')
        return '1';
    return a == 'U' || b == 'U' ? 'U' : 'X';
}
char orBit(char a, char b) noexcept {
    if (a == '1' || b == '1')
        return '1';
    if (a == '0' && b == '0')
        return '0';
    return a == 'U' || b == 'U' ? 'U' : 'X';
}
char xorBit(char a, char b) noexcept {
    if ((a == '0' || a == '1') && (b == '0' || b == '1'))
        return a == b ? '0' : '1';
    return a == 'U' || b == 'U' ? 'U' : 'X';
}
char notBit(char a) noexcept {
    return a == '0' ? '1' : a == '1' ? '0' : a;
}

/// Exact bit class for === and !==: L is 0, H is 1, the other letters stand for themselves.
char exactBit(char c) noexcept {
    return c == 'L' ? '0' : c == 'H' ? '1' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

/// '1' true, '0' false, 'X' or 'U' unknown.
char truthOfBits(const std::string& s) noexcept {
    bool unknown = false, u = false;
    for (char c : s) {
        const char n = norm(c);
        if (n == '1')
            return '1';
        if (n != '0') {
            unknown = true;
            u = u || n == 'U';
        }
    }
    return !unknown ? '0' : u ? 'U' : 'X';
}

bool allKnown(const std::string& s) noexcept {
    for (char c : s) {
        const char n = norm(c);
        if (n != '0' && n != '1')
            return false;
    }
    return true;
}

/// Numeric value of known bits (unsigned or two's complement).
double bitsToDouble(const std::string& s, bool sgn) noexcept {
    double v = 0.0;
    for (char c : s)
        v = v * 2.0 + (norm(c) == '1' ? 1.0 : 0.0);
    if (sgn && !s.empty() && norm(s[0]) == '1')
        v -= std::ldexp(1.0, static_cast<int>(s.size()));
    return v;
}

std::string toLower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool equalsNoCase(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

std::string uintToBits(std::uint64_t v, std::uint32_t width) {
    std::string out(width, '0');
    for (std::uint32_t i = 0; i < width && i < 64; ++i)
        if ((v >> i) & 1u)
            out[width - 1 - i] = '1';
    return out;
}

std::uint32_t bitLength(std::uint64_t v) noexcept {
    std::uint32_t n = 1;
    while (n < 64 && (v >> n) != 0)
        ++n;
    return n;
}

// ---- Lexer -----------------------------------------------------------------------------------------------

enum class Tok : std::uint8_t { End, Name, Int, Bits, Real, Str, Op };

struct Token {
    Tok kind = Tok::End;
    std::string
        text; ///< Name: the name; Op: the operator (keywords lower case); Bits: MSB first; Str: content
    std::uint64_t ival = 0;
    double rval = 0.0;
    int col = 1;
};

bool isNameStart(char c) noexcept {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '/';
}
bool isNameChar(char c) noexcept {
    // '*' and '?' are taken in so that the resolver can explain that wildcards are not allowed
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '/' || c == '.' || c == '*' ||
           c == '?';
}

const char* const kKeywords[] = {"and", "or", "xor", "nand", "nor", "xnor", "not", "downto", "to"};

/// Appends the bits of one digit of a base-2/8/16 literal. False for a digit that is not valid.
bool appendDigit(std::string& bits, char d, int bitsPerDigit) {
    const char u = static_cast<char>(std::toupper(static_cast<unsigned char>(d)));
    if (u == 'X' || u == 'Z' || u == 'U' || u == '-' || u == 'W' || u == 'L' || u == 'H') {
        if (bitsPerDigit != 1 && (u == 'L' || u == 'H' || u == 'W'))
            return false;
        bits.append(static_cast<std::size_t>(bitsPerDigit), u);
        return true;
    }
    int v = -1;
    if (u >= '0' && u <= '9')
        v = u - '0';
    else if (u >= 'A' && u <= 'F')
        v = u - 'A' + 10;
    if (v < 0 || v >= (1 << bitsPerDigit))
        return false;
    for (int b = bitsPerDigit - 1; b >= 0; --b)
        bits += ((v >> b) & 1) ? '1' : '0';
    return true;
}

class Lexer {
public:
    explicit Lexer(std::string_view s)
        : s_(s) {}

    Result<std::vector<Token>> run() {
        using R = Result<std::vector<Token>>;
        std::vector<Token> out;
        while (true) {
            while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_])))
                ++i_;
            Token t;
            t.col = static_cast<int>(i_) + 1;
            if (i_ >= s_.size()) {
                out.push_back(t);
                return out;
            }
            std::string err;
            if (!next(t, err))
                return R::failure("col " + std::to_string(t.col) + ": " + err);
            out.push_back(std::move(t));
        }
    }

private:
    char peek(std::size_t k = 0) const noexcept { return i_ + k < s_.size() ? s_[i_ + k] : '\0'; }

    bool basedDigits(Token& t, int bitsPerDigit, char terminator, std::string& err) {
        std::string bits;
        bool any = false;
        while (i_ < s_.size() && s_[i_] != terminator) {
            const char c = s_[i_];
            if (c == '_') {
                ++i_;
                continue;
            }
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-')
                break;
            if (!appendDigit(bits, c, bitsPerDigit)) {
                err = std::string("bad digit '") + c + "'";
                return false;
            }
            any = true;
            ++i_;
        }
        if (!any) {
            err = "digits expected";
            return false;
        }
        t.kind = Tok::Bits;
        t.text = std::move(bits);
        return true;
    }

    bool number(Token& t, std::string& err) {
        const std::size_t start = i_;
        if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X' || peek(1) == 'b' || peek(1) == 'B' ||
                              peek(1) == 'o' || peek(1) == 'O')) {
            const char k = static_cast<char>(std::tolower(static_cast<unsigned char>(peek(1))));
            i_ += 2;
            return basedDigits(t, k == 'x' ? 4 : k == 'o' ? 3 : 1, '\0', err);
        }
        std::string digits;
        while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_') {
            if (peek() != '_')
                digits += peek();
            ++i_;
        }
        if (peek() == '#') { // VHDL based literal 16#3F# (the closing # may be left out)
            const int base = std::atoi(digits.c_str());
            ++i_;
            if (base == 2 || base == 8 || base == 16) {
                if (!basedDigits(t, base == 16 ? 4 : base == 8 ? 3 : 1, '#', err))
                    return false;
            } else if (base >= 2 && base <= 16) {
                std::uint64_t v = 0;
                bool any = false;
                while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') {
                    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(peek())));
                    ++i_;
                    if (c == '_')
                        continue;
                    const int d = std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : c - 'A' + 10;
                    if (d < 0 || d >= base) {
                        err = std::string("bad digit '") + c + "'";
                        return false;
                    }
                    v = v * static_cast<std::uint64_t>(base) + static_cast<std::uint64_t>(d);
                    any = true;
                }
                if (!any) {
                    err = "digits expected";
                    return false;
                }
                t.kind = Tok::Int;
                t.ival = v;
            } else {
                err = "base must be 2 to 16";
                return false;
            }
            if (peek() == '#')
                ++i_;
            return true;
        }
        if (peek() == '\'' && std::isalpha(static_cast<unsigned char>(peek(1)))) { // Verilog 8'h3F
            const unsigned long width = std::strtoul(digits.c_str(), nullptr, 10);
            ++i_;
            if (!verilogBased(t, err))
                return false;
            if (width == 0 || width > 4096) {
                err = "bad width";
                return false;
            }
            fitWidth(t.text, static_cast<std::uint32_t>(width));
            return true;
        }
        const bool fraction = peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1)));
        const bool exponent =
            (peek() == 'e' || peek() == 'E') &&
            (std::isdigit(static_cast<unsigned char>(peek(1))) ||
             ((peek(1) == '-' || peek(1) == '+') && std::isdigit(static_cast<unsigned char>(peek(2)))));
        if (fraction || exponent) {
            if (fraction) {
                ++i_;
                while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_')
                    ++i_;
            }
            if (peek() == 'e' || peek() == 'E') {
                ++i_;
                if (peek() == '-' || peek() == '+')
                    ++i_;
                while (std::isdigit(static_cast<unsigned char>(peek())))
                    ++i_;
            }
            std::string text(s_.substr(start, i_ - start));
            text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
            t.kind = Tok::Real;
            t.rval = std::strtod(text.c_str(), nullptr);
            return true;
        }
        if (digits.size() > 20 || (digits.size() == 20 && digits > "18446744073709551615")) {
            err = "number too large (use a based literal such as 16#...#)";
            return false;
        }
        t.kind = Tok::Int;
        t.ival = std::strtoull(digits.c_str(), nullptr, 10);
        return true;
    }

    // After the quote of 'h3F / 'b101 / 'd10 (optionally 's').
    bool verilogBased(Token& t, std::string& err) {
        if (peek() == 's' || peek() == 'S')
            ++i_;
        const char k = static_cast<char>(std::tolower(static_cast<unsigned char>(peek())));
        ++i_;
        if (k == 'd') {
            std::string digits;
            while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_') {
                if (peek() != '_')
                    digits += peek();
                ++i_;
            }
            if (digits.empty() || digits.size() > 19) {
                err = "bad decimal digits";
                return false;
            }
            const std::uint64_t v = std::strtoull(digits.c_str(), nullptr, 10);
            t.kind = Tok::Bits;
            t.text = uintToBits(v, bitLength(v));
            return true;
        }
        if (k != 'h' && k != 'b' && k != 'o') {
            err = "expected h, b, o or d after the quote";
            return false;
        }
        return basedDigits(t, k == 'h' ? 4 : k == 'o' ? 3 : 1, '\0', err);
    }

    static void fitWidth(std::string& bits, std::uint32_t width) {
        if (bits.size() > width) {
            bits.erase(0, bits.size() - width);
        } else if (bits.size() < width) {
            const char lead = !bits.empty() && (bits[0] == 'X' || bits[0] == 'Z') ? bits[0] : '0';
            bits.insert(0, width - bits.size(), lead);
        }
    }

    bool next(Token& t, std::string& err) {
        const char c = peek();
        if (std::isdigit(static_cast<unsigned char>(c)))
            return number(t, err);
        if (c == '\'') {
            if (peek(2) == '\'' && peek(1) != '\0') { // '1'
                std::string b;
                if (!appendDigit(b, peek(1), 1)) {
                    err = std::string("bad bit '") + peek(1) + "'";
                    return false;
                }
                t.kind = Tok::Bits;
                t.text = b;
                i_ += 3;
                return true;
            }
            ++i_;
            return verilogBased(t, err); // unsized 'h3F
        }
        if (c == '"') {
            ++i_;
            const std::size_t start = i_;
            while (i_ < s_.size() && s_[i_] != '"')
                ++i_;
            if (i_ >= s_.size()) {
                err = "unterminated string";
                return false;
            }
            t.kind = Tok::Str;
            t.text = std::string(s_.substr(start, i_ - start));
            ++i_;
            return true;
        }
        // x"3F" b"0101" o"17"
        if ((c == 'x' || c == 'X' || c == 'b' || c == 'B' || c == 'o' || c == 'O') && peek(1) == '"') {
            const char k = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            i_ += 2;
            if (!basedDigits(t, k == 'x' ? 4 : k == 'o' ? 3 : 1, '"', err))
                return false;
            if (peek() != '"') {
                err = "unterminated bit string";
                return false;
            }
            ++i_;
            return true;
        }
        if (c == '/' && peek(1) == '=') {
            t.kind = Tok::Op;
            t.text = "/=";
            i_ += 2;
            return true;
        }
        if (isNameStart(c)) {
            const std::size_t start = i_;
            while (isNameChar(peek()))
                ++i_;
            // a dataset prefix: ref:/tb/x or ref:tb.x
            if (peek() == ':' && (isNameStart(peek(1))) &&
                s_.substr(start, i_ - start).find_first_of("/.") == std::string_view::npos) {
                ++i_;
                while (isNameChar(peek()))
                    ++i_;
            }
            std::string word(s_.substr(start, i_ - start));
            const std::string low = toLower(word);
            for (const char* k : kKeywords)
                if (low == k) {
                    t.kind = Tok::Op;
                    t.text = low;
                    return true;
                }
            if (low == "true" || low == "false") {
                t.kind = Tok::Bits;
                t.text = low == "true" ? "1" : "0";
                return true;
            }
            t.kind = Tok::Name;
            t.text = std::move(word);
            return true;
        }
        static const char* const kOps[] = {"===", "!==", "==", "!=", "<=", ">=", "&&", "||", "<",
                                           ">",   "=",   "&",  "|",  "^",  "~",  "!",  "+",  "-",
                                           "(",   ")",   "[",  "]",  "{",  "}",  ",",  ":"};
        for (const char* op : kOps) {
            const std::string_view v(op);
            if (s_.substr(i_, v.size()) == v) {
                t.kind = Tok::Op;
                t.text = op;
                i_ += v.size();
                return true;
            }
        }
        err = std::string("unexpected character '") + c + "'";
        return false;
    }

    std::string_view s_;
    std::size_t i_ = 0;
};

// ---- Parser: tokens to a syntax tree --------------------------------------------------------------------

struct Ast {
    enum Kind : std::uint8_t { Name, Int, Bits, Real, Str, Unary, Binary, Slice, Concat, Call } kind = Name;
    std::string text; ///< Name, Bits (MSB first), Str; the operator for Unary / Binary; the function for Call
    std::uint64_t ival = 0;
    double rval = 0.0;
    std::vector<int> kids;
    std::int64_t hi = 0, lo = 0; ///< Slice
    int col = 1;
};

class Parser {
public:
    explicit Parser(std::vector<Token> toks)
        : t_(std::move(toks)) {}

    Result<int> run() {
        const int root = parseOr();
        if (!err_.empty())
            return Result<int>::failure(err_);
        if (cur().kind != Tok::End)
            return Result<int>::failure("col " + std::to_string(cur().col) + ": unexpected '" +
                                        tokText(cur()) + "'");
        return root;
    }
    std::vector<Ast>& nodes() { return n_; }

private:
    static std::string tokText(const Token& t) {
        switch (t.kind) {
        case Tok::Int:
            return std::to_string(t.ival);
        case Tok::Real:
            return "number";
        case Tok::Str:
            return "\"" + t.text + "\"";
        default:
            return t.text;
        }
    }
    const Token& cur() const { return t_[std::min(p_, t_.size() - 1)]; }
    bool isOp(const char* op) const { return cur().kind == Tok::Op && cur().text == op; }
    int fail(const std::string& msg) {
        if (err_.empty())
            err_ = "col " + std::to_string(cur().col) + ": " + msg;
        return -1;
    }
    int add(Ast a) {
        n_.push_back(std::move(a));
        return static_cast<int>(n_.size()) - 1;
    }
    int binary(const std::string& op, int a, int b, int col) {
        Ast x;
        x.kind = Ast::Binary;
        x.text = op;
        x.kids = {a, b};
        x.col = col;
        return add(std::move(x));
    }

    template <class Next>
    int level(std::initializer_list<const char*> ops, Next next) {
        int left = (this->*next)();
        while (err_.empty()) {
            const char* found = nullptr;
            for (const char* op : ops)
                if (isOp(op))
                    found = op;
            if (found == nullptr)
                break;
            const int col = cur().col;
            ++p_;
            const int right = (this->*next)();
            if (!err_.empty())
                return -1;
            left = binary(found, left, right, col);
        }
        return left;
    }

    int parseOr() { return level({"or", "||", "|", "nor"}, &Parser::parseXor); }
    int parseXor() { return level({"xor", "^", "xnor"}, &Parser::parseAnd); }
    int parseAnd() { return level({"and", "&&", "nand"}, &Parser::parseRel); }
    int parseRel() {
        const int left = parseAdd();
        if (!err_.empty())
            return -1;
        static const char* const kRel[] = {"===", "!==", "==", "!=", "/=", "<=", ">=", "<", ">", "="};
        for (const char* op : kRel)
            if (isOp(op)) {
                const int col = cur().col;
                ++p_;
                const int right = parseAdd();
                if (!err_.empty())
                    return -1;
                return binary(op, left, right, col);
            }
        return left;
    }
    int parseAdd() { return level({"+", "-", "&"}, &Parser::parseUnary); }

    int parseUnary() {
        for (const char* op : {"not", "~", "!", "-", "+"})
            if (isOp(op)) {
                const int col = cur().col;
                ++p_;
                const int a = parseUnary();
                if (!err_.empty())
                    return -1;
                if (std::string(op) == "+")
                    return a;
                Ast x;
                x.kind = Ast::Unary;
                x.text = op;
                x.kids = {a};
                x.col = col;
                return add(std::move(x));
            }
        return parsePostfix();
    }

    bool intToken(std::int64_t& out) {
        if (cur().kind != Tok::Int) {
            fail("bit index expected");
            return false;
        }
        if (cur().ival > 1u << 20) {
            fail("bit index too large");
            return false;
        }
        out = static_cast<std::int64_t>(cur().ival);
        ++p_;
        return true;
    }

    int parsePostfix() {
        int base = parsePrimary();
        while (err_.empty() && (isOp("(") || isOp("["))) {
            const bool paren = isOp("(");
            const int col = cur().col;
            ++p_;
            Ast x;
            x.kind = Ast::Slice;
            x.kids = {base};
            x.col = col;
            if (!intToken(x.hi))
                return -1;
            x.lo = x.hi;
            if (paren && (isOp("downto") || isOp("to"))) {
                const bool to = isOp("to");
                ++p_;
                if (!intToken(x.lo))
                    return -1;
                if (to)
                    std::swap(x.hi, x.lo);
            } else if (!paren && isOp(":")) {
                ++p_;
                if (!intToken(x.lo))
                    return -1;
            }
            if (!isOp(paren ? ")" : "]"))
                return fail(paren ? "')' expected" : "']' expected");
            ++p_;
            base = add(std::move(x));
        }
        return base;
    }

    int parsePrimary() {
        const Token& t = cur();
        Ast x;
        x.col = t.col;
        switch (t.kind) {
        case Tok::End:
            return fail("expression incomplete");
        case Tok::Int:
            x.kind = Ast::Int;
            x.ival = t.ival;
            ++p_;
            return add(std::move(x));
        case Tok::Bits:
            x.kind = Ast::Bits;
            x.text = t.text;
            ++p_;
            return add(std::move(x));
        case Tok::Real:
            x.kind = Ast::Real;
            x.rval = t.rval;
            ++p_;
            return add(std::move(x));
        case Tok::Str:
            x.kind = Ast::Str;
            x.text = t.text;
            ++p_;
            return add(std::move(x));
        case Tok::Name: {
            const std::string low = toLower(t.text);
            if ((low == "signed" || low == "unsigned") && p_ + 1 < t_.size() && t_[p_ + 1].kind == Tok::Op &&
                t_[p_ + 1].text == "(") {
                p_ += 2;
                const int a = parseOr();
                if (!err_.empty())
                    return -1;
                if (!isOp(")"))
                    return fail("')' expected");
                ++p_;
                x.kind = Ast::Call;
                x.text = low;
                x.kids = {a};
                return add(std::move(x));
            }
            x.kind = Ast::Name;
            x.text = t.text;
            ++p_;
            return add(std::move(x));
        }
        case Tok::Op:
            break;
        }
        if (isOp("(")) {
            ++p_;
            const int a = parseOr();
            if (!err_.empty())
                return -1;
            if (!isOp(")"))
                return fail("')' expected");
            ++p_;
            return a;
        }
        if (isOp("{")) {
            ++p_;
            x.kind = Ast::Concat;
            while (true) {
                const int a = parseOr();
                if (!err_.empty())
                    return -1;
                x.kids.push_back(a);
                if (isOp(",")) {
                    ++p_;
                    continue;
                }
                if (!isOp("}"))
                    return fail("'}' or ',' expected");
                ++p_;
                break;
            }
            return add(std::move(x));
        }
        return fail("unexpected '" + tokText(t) + "'");
    }

    std::vector<Token> t_;
    std::size_t p_ = 0;
    std::vector<Ast> n_;
    std::string err_;
};

enum class Op : std::uint8_t {
    Input,
    Const,
    Not,
    And,
    Or,
    Xor,
    Nand,
    Nor,
    Xnor,
    LAnd,
    LOr,
    LNot,
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,
    CaseEq,
    CaseNe,
    Add,
    Sub,
    Neg,
    Concat,
    Slice,
    Pass,
};

} // namespace

struct Expression::Node {
    Op op = Op::Const;
    std::vector<int> kids;
    ExprType type = ExprType::Bits;
    std::uint32_t width = 1;
    bool isSigned = false;
    int input = -1;        ///< Input: index into inputs_
    std::uint32_t pos = 0; ///< Slice: MSB-first position of the first bit taken
    std::string bits;
    double real = 0.0;
    std::string text;
    bool dirty = true;
};

Expression::Expression() = default;
Expression::~Expression() = default;
Expression::Expression(Expression&&) noexcept = default;
Expression& Expression::operator=(Expression&&) noexcept = default;

SignalKind Expression::resultKind() const noexcept {
    return type_ == ExprType::Real   ? SignalKind::Real
           : type_ == ExprType::Text ? SignalKind::String
                                     : SignalKind::Logic;
}

std::string Expression::describe() const {
    switch (type_) {
    case ExprType::Real:
        return "real";
    case ExprType::Text:
        return "text";
    case ExprType::Bits:
        break;
    }
    return width_ == 1 ? "1 bit" : std::to_string(width_) + " bit vector";
}

// ---- Binding: syntax tree to typed nodes ------------------------------------------------------------------

class ExprCompiler {
public:
    ExprCompiler(Expression& e, std::vector<Ast>& ast, const SignalResolver& r)
        : e_(e)
        , a_(ast)
        , resolve_(r) {}

    using Node = Expression::Node;

    /// A pending operand of a relation: a string literal or a bare unknown word, typed by the other side.
    struct Bound {
        int node = -1;
        int pendingAst = -1; ///< >= 0: not bound yet (Str literal or unresolved simple name)
    };

    int fail(int col, const std::string& msg) {
        if (err_.empty())
            err_ = "col " + std::to_string(col) + ": " + msg;
        return -1;
    }
    const std::string& error() const { return err_; }

    int push(Node n) {
        e_.nodes_.push_back(std::move(n));
        return static_cast<int>(e_.nodes_.size()) - 1;
    }
    Node& at(int i) { return e_.nodes_[static_cast<std::size_t>(i)]; }

    int constBits(std::string bits, bool isSigned = false) {
        Node n;
        n.op = Op::Const;
        n.type = ExprType::Bits;
        n.width = static_cast<std::uint32_t>(bits.size());
        n.bits = std::move(bits);
        n.isSigned = isSigned;
        return push(std::move(n));
    }
    int constText(std::string text) {
        Node n;
        n.op = Op::Const;
        n.type = ExprType::Text;
        n.text = std::move(text);
        return push(std::move(n));
    }

    int bitsFromString(const Ast& x) {
        std::string bits;
        for (char c : x.text) {
            if (c == '_')
                continue;
            if (!appendDigit(bits, c, 1))
                return fail(x.col, "\"" + x.text +
                                       "\" is not a bit string (compare text with an enumeration signal)");
        }
        if (bits.empty())
            return fail(x.col, "empty bit string");
        return constBits(std::move(bits));
    }

    static bool simpleWord(const std::string& s) {
        return !s.empty() && s.find_first_of("/.:") == std::string::npos;
    }

    int name(const Ast& x) {
        std::string err;
        const Signal* s = resolve_ ? resolve_(x.text, err) : nullptr;
        if (s == nullptr)
            return fail(x.col, err.empty() ? "unknown signal '" + x.text + "'" : err);
        int idx = -1;
        for (std::size_t i = 0; i < e_.inputs_.size(); ++i)
            if (e_.inputs_[i] == s)
                idx = static_cast<int>(i);
        if (idx < 0) {
            e_.inputs_.push_back(s);
            idx = static_cast<int>(e_.inputs_.size()) - 1;
        }
        Node n;
        n.op = Op::Input;
        n.input = idx;
        n.type = s->kind() == SignalKind::Real     ? ExprType::Real
                 : s->kind() == SignalKind::String ? ExprType::Text
                                                   : ExprType::Bits;
        n.width = s->kind() == SignalKind::Logic ? std::max<std::uint32_t>(1, s->width()) : 1;
        return push(std::move(n));
    }

    Bound bindLenient(int ai) {
        const Ast& x = a_[static_cast<std::size_t>(ai)];
        if (x.kind == Ast::Str)
            return {-1, ai};
        if (x.kind == Ast::Name && simpleWord(x.text)) {
            std::string err;
            if (resolve_ == nullptr || resolve_(x.text, err) == nullptr)
                return {-1, ai};
        }
        return {bind(ai), -1};
    }

    int settle(const Bound& b, bool textContext) {
        if (b.pendingAst < 0)
            return b.node;
        const Ast& x = a_[static_cast<std::size_t>(b.pendingAst)];
        if (textContext)
            return constText(x.text);
        if (x.kind == Ast::Str)
            return bitsFromString(x);
        return name(x); // reports the unknown signal
    }

    int bind(int ai) {
        if (!err_.empty())
            return -1;
        const Ast& x = a_[static_cast<std::size_t>(ai)];
        switch (x.kind) {
        case Ast::Name:
            return name(x);
        case Ast::Int: {
            Node n;
            n.op = Op::Const;
            n.width = bitLength(x.ival);
            n.bits = uintToBits(x.ival, n.width);
            n.real = static_cast<double>(x.ival);
            return push(std::move(n));
        }
        case Ast::Bits:
            return constBits(x.text);
        case Ast::Real: {
            Node n;
            n.op = Op::Const;
            n.type = ExprType::Real;
            n.real = x.rval;
            return push(std::move(n));
        }
        case Ast::Str:
            return bitsFromString(x);
        case Ast::Call: {
            const int a = bind(x.kids[0]);
            if (a < 0)
                return -1;
            if (at(a).type != ExprType::Bits)
                return fail(x.col, x.text + "() needs a bit vector");
            Node n;
            n.op = Op::Pass;
            n.kids = {a};
            n.width = at(a).width;
            n.isSigned = x.text == "signed";
            return push(std::move(n));
        }
        case Ast::Slice: {
            const int a = bind(x.kids[0]);
            if (a < 0)
                return -1;
            const Node& src = at(a);
            if (src.type != ExprType::Bits)
                return fail(x.col, "only bit vectors can be indexed");
            if (x.hi < x.lo)
                return fail(x.col, "empty range");
            if (static_cast<std::uint64_t>(x.hi) >= src.width)
                return fail(x.col, "bit " + std::to_string(x.hi) + " is outside the " +
                                       std::to_string(src.width) +
                                       " bit value (bit 0 is the least significant)");
            Node n;
            n.op = Op::Slice;
            n.kids = {a};
            n.width = static_cast<std::uint32_t>(x.hi - x.lo + 1);
            n.pos = src.width - 1 - static_cast<std::uint32_t>(x.hi);
            return push(std::move(n));
        }
        case Ast::Concat: {
            Node n;
            n.op = Op::Concat;
            n.width = 0;
            for (int k : x.kids) {
                const int c = bind(k);
                if (c < 0)
                    return -1;
                if (at(c).type != ExprType::Bits)
                    return fail(x.col, "only bit vectors can be concatenated");
                n.kids.push_back(c);
                n.width += at(c).width;
            }
            return push(std::move(n));
        }
        case Ast::Unary:
            return unary(x);
        case Ast::Binary:
            return binaryNode(x);
        }
        return -1;
    }

    int unary(const Ast& x) {
        const Ast& operand = a_[static_cast<std::size_t>(x.kids[0])];
        const int a = bind(x.kids[0]);
        if (a < 0)
            return -1;
        const Node& src = at(a);
        Node n;
        n.kids = {a};
        if (x.text == "-") {
            if (src.type == ExprType::Text)
                return fail(x.col, "cannot negate text");
            n.op = Op::Neg;
            n.type = src.type;
            n.width = src.width;
            n.isSigned = true;
            if (src.type == ExprType::Bits && src.op == Op::Const && operand.kind == Ast::Int) {
                // -5: a signed constant with room for the sign
                const std::uint32_t w = src.width + 1;
                std::string bits = "0" + src.bits;
                std::string out(w, '0');
                int carry = 1;
                for (std::uint32_t i = w; i-- > 0;) {
                    const int b = (bits[i] == '1' ? 0 : 1) + carry;
                    out[i] = (b & 1) ? '1' : '0';
                    carry = b >> 1;
                }
                e_.nodes_.pop_back(); // the positive constant is not needed
                return constBits(std::move(out), true);
            }
            return push(std::move(n));
        }
        if (src.type != ExprType::Bits && x.text != "!")
            return fail(x.col, "'" + x.text + "' needs a bit vector");
        if (x.text == "!") {
            if (src.type == ExprType::Text)
                return fail(x.col, "'!' needs a bit vector or a number");
            n.op = Op::LNot;
            n.width = 1;
            return push(std::move(n));
        }
        n.op = Op::Not;
        n.width = src.width;
        n.isSigned = src.isSigned;
        return push(std::move(n));
    }

    int binaryNode(const Ast& x) {
        const std::string& op = x.text;
        static const char* const kRel[] = {"=", "==", "/=", "!=", "<", "<=", ">", ">=", "===", "!=="};
        bool rel = false;
        for (const char* r : kRel)
            rel = rel || op == r;

        int a = -1, b = -1;
        if (rel) {
            const Bound la = bindLenient(x.kids[0]);
            if (!err_.empty())
                return -1;
            const Bound lb = bindLenient(x.kids[1]);
            if (!err_.empty())
                return -1;
            const bool textA = la.node >= 0 && at(la.node).type == ExprType::Text;
            const bool textB = lb.node >= 0 && at(lb.node).type == ExprType::Text;
            a = settle(la, textB);
            if (a < 0)
                return -1;
            b = settle(lb, textA);
            if (b < 0)
                return -1;
        } else {
            a = bind(x.kids[0]);
            if (a < 0)
                return -1;
            b = bind(x.kids[1]);
            if (b < 0)
                return -1;
        }
        const Node& na = at(a);
        const Node& nb = at(b);
        Node n;
        n.kids = {a, b};

        if (rel) {
            n.width = 1;
            n.op = op == "=" || op == "=="    ? Op::Eq
                   : op == "/=" || op == "!=" ? Op::Ne
                   : op == "<"                ? Op::Lt
                   : op == "<="               ? Op::Le
                   : op == ">"                ? Op::Gt
                   : op == ">="               ? Op::Ge
                   : op == "==="              ? Op::CaseEq
                                              : Op::CaseNe;
            if ((na.type == ExprType::Text) != (nb.type == ExprType::Text))
                return fail(x.col, "text can only be compared with text");
            if (na.type == ExprType::Text && n.op != Op::Eq && n.op != Op::Ne && n.op != Op::CaseEq &&
                n.op != Op::CaseNe)
                return fail(x.col, "text can only be compared for equality");
            return push(std::move(n));
        }
        if (op == "&&" || op == "||") {
            if (na.type == ExprType::Text || nb.type == ExprType::Text)
                return fail(x.col, "'" + op + "' needs bit vectors or numbers");
            n.op = op == "&&" ? Op::LAnd : Op::LOr;
            n.width = 1;
            return push(std::move(n));
        }
        if (op == "+" || op == "-") {
            if (na.type == ExprType::Text || nb.type == ExprType::Text)
                return fail(x.col, "cannot add or subtract text");
            n.op = op == "+" ? Op::Add : Op::Sub;
            if (na.type == ExprType::Real || nb.type == ExprType::Real) {
                n.type = ExprType::Real;
                return push(std::move(n));
            }
            const bool mixed = na.isSigned != nb.isSigned;
            n.isSigned = na.isSigned || nb.isSigned;
            n.width = std::max(na.width + (mixed && !na.isSigned ? 1 : 0),
                               nb.width + (mixed && !nb.isSigned ? 1 : 0));
            return push(std::move(n));
        }
        if (op == "&") {
            if (na.type != ExprType::Bits || nb.type != ExprType::Bits)
                return fail(x.col, "only bit vectors can be concatenated");
            n.op = Op::Concat;
            n.width = na.width + nb.width;
            return push(std::move(n));
        }
        // bitwise
        if (na.type != ExprType::Bits || nb.type != ExprType::Bits)
            return fail(x.col, "'" + op + "' needs bit vectors");
        n.op = op == "and"    ? Op::And
               : op == "or"   ? Op::Or
               : op == "|"    ? Op::Or
               : op == "xor"  ? Op::Xor
               : op == "^"    ? Op::Xor
               : op == "nand" ? Op::Nand
               : op == "nor"  ? Op::Nor
                              : Op::Xnor;
        n.width = std::max(na.width, nb.width);
        n.isSigned = na.isSigned && nb.isSigned;
        return push(std::move(n));
    }

private:
    Expression& e_;
    std::vector<Ast>& a_;
    const SignalResolver& resolve_;
    std::string err_;
};

Result<Expression> Expression::compile(std::string_view text, const SignalResolver& resolve) {
    using R = Result<Expression>;
    auto toks = Lexer(text).run();
    if (!toks.ok())
        return R::failure(toks.error());
    if (toks.value().size() == 1)
        return R::failure("empty expression");
    Parser p(std::move(toks).value());
    const auto root = p.run();
    if (!root.ok())
        return R::failure(root.error());

    Expression e;
    e.text_ = std::string(text);
    ExprCompiler c(e, p.nodes(), resolve);
    const int top = c.bind(root.value());
    if (top < 0)
        return R::failure(c.error());
    if (e.inputs_.empty())
        return R::failure("the expression reads no signal");
    // The result must be the last node: bind pushes parents after children, and the root is bound last.
    if (top != static_cast<int>(e.nodes_.size()) - 1)
        return R::failure("internal error: result is not the last node");
    e.type_ = e.nodes_.back().type;
    e.width_ = e.type_ == ExprType::Bits ? e.nodes_.back().width : 1;
    return e;
}

// ---- Evaluation -------------------------------------------------------------------------------------------

namespace {

/// Compares two known bit vectors as numbers: -1, 0, 1.
int compareKnown(const std::string& a, bool sa, const std::string& b, bool sb) {
    const bool mixed = sa != sb;
    const std::uint32_t w = static_cast<std::uint32_t>(
        std::max(a.size() + (mixed && !sa ? 1 : 0), b.size() + (mixed && !sb ? 1 : 0)));
    const bool sgn = sa || sb;
    // extension: the unsigned side gets zeros, the signed side its sign
    for (std::uint32_t i = 0; i < w; ++i) {
        const char ca = norm(extAt(a, w, i, sa));
        const char cb = norm(extAt(b, w, i, sb));
        if (ca == cb)
            continue;
        const int r = ca < cb ? -1 : 1;
        return (i == 0 && sgn) ? -r : r; // a set sign bit means smaller
    }
    return 0;
}

double numberOf(const Expression::Node& n, bool& known) {
    if (n.type == ExprType::Real) {
        known = !std::isnan(n.real);
        return n.real;
    }
    known = allKnown(n.bits);
    return known ? bitsToDouble(n.bits, n.isSigned) : 0.0;
}

char truthOf(const Expression::Node& n) {
    if (n.type == ExprType::Real)
        return std::isnan(n.real) ? 'X' : (n.real != 0.0 ? '1' : '0');
    return truthOfBits(n.bits);
}

void addBits(const std::string& a, bool sa, const std::string& b, bool sb, bool subtract, std::uint32_t w,
             std::string& out) {
    out.assign(w, '0');
    if (!allKnown(a) || !allKnown(b)) {
        out.assign(w, 'X');
        return;
    }
    int carry = subtract ? 1 : 0;
    for (std::uint32_t i = w; i-- > 0;) {
        const int x = norm(extAt(a, w, i, sa)) == '1' ? 1 : 0;
        int y = norm(extAt(b, w, i, sb)) == '1' ? 1 : 0;
        if (subtract)
            y ^= 1;
        const int s = x + y + carry;
        out[i] = (s & 1) ? '1' : '0';
        carry = s >> 1;
    }
}

} // namespace

Expression::Value Expression::evaluate(const std::vector<std::size_t>& index,
                                       const std::vector<char>& changed) {
    for (Node& n : nodes_) {
        switch (n.op) {
        case Op::Input:
            n.dirty = first_ || changed[static_cast<std::size_t>(n.input)] != 0;
            break;
        case Op::Const:
            n.dirty = first_;
            continue; // the value is set at bind time
        default: {
            n.dirty = false;
            for (int k : n.kids)
                n.dirty = n.dirty || nodes_[static_cast<std::size_t>(k)].dirty;
        }
        }
        if (!n.dirty)
            continue;

        auto kid = [&](std::size_t k) -> const Node& { return nodes_[static_cast<std::size_t>(n.kids[k])]; };
        switch (n.op) {
        case Op::Input: {
            const Signal& s = *inputs_[static_cast<std::size_t>(n.input)];
            const std::size_t i = index[static_cast<std::size_t>(n.input)];
            if (n.type == ExprType::Real) {
                n.real = i == kNoIndex ? std::numeric_limits<double>::quiet_NaN() : s.real(i);
            } else if (n.type == ExprType::Text) {
                if (i == kNoIndex)
                    n.text.clear();
                else
                    n.text.assign(s.text(i));
            } else {
                n.bits.resize(n.width);
                if (i == kNoIndex) {
                    std::fill(n.bits.begin(), n.bits.end(), 'U');
                } else {
                    for (std::uint32_t b = 0; b < n.width; ++b)
                        n.bits[n.width - 1 - b] = toChar(s.bit(i, b));
                }
            }
            break;
        }
        case Op::Const:
            break;
        case Op::Pass:
            n.bits = kid(0).bits;
            break;
        case Op::Slice:
            n.bits.assign(kid(0).bits, n.pos, n.width);
            break;
        case Op::Concat:
            n.bits.clear();
            for (std::size_t k = 0; k < n.kids.size(); ++k)
                n.bits += kid(k).bits;
            break;
        case Op::Not:
            n.bits.resize(n.width);
            for (std::uint32_t i = 0; i < n.width; ++i)
                n.bits[i] = notBit(norm(kid(0).bits[i]));
            break;
        case Op::And:
        case Op::Or:
        case Op::Xor:
        case Op::Nand:
        case Op::Nor:
        case Op::Xnor: {
            const Node& a = kid(0);
            const Node& b = kid(1);
            n.bits.resize(n.width);
            for (std::uint32_t i = 0; i < n.width; ++i) {
                const char x = norm(extAt(a.bits, n.width, i, a.isSigned));
                const char y = norm(extAt(b.bits, n.width, i, b.isSigned));
                char r = 'X';
                switch (n.op) {
                case Op::And:
                    r = andBit(x, y);
                    break;
                case Op::Or:
                    r = orBit(x, y);
                    break;
                case Op::Xor:
                    r = xorBit(x, y);
                    break;
                case Op::Nand:
                    r = notBit(andBit(x, y));
                    break;
                case Op::Nor:
                    r = notBit(orBit(x, y));
                    break;
                default:
                    r = notBit(xorBit(x, y));
                    break;
                }
                n.bits[i] = r;
            }
            break;
        }
        case Op::LNot: {
            const char t = truthOf(kid(0));
            n.bits.assign(1, notBit(t));
            break;
        }
        case Op::LAnd:
            n.bits.assign(1, andBit(truthOf(kid(0)), truthOf(kid(1))));
            break;
        case Op::LOr:
            n.bits.assign(1, orBit(truthOf(kid(0)), truthOf(kid(1))));
            break;
        case Op::Eq:
        case Op::Ne:
        case Op::Lt:
        case Op::Le:
        case Op::Gt:
        case Op::Ge:
        case Op::CaseEq:
        case Op::CaseNe: {
            const Node& a = kid(0);
            const Node& b = kid(1);
            char r = 'X';
            const bool exact = n.op == Op::CaseEq || n.op == Op::CaseNe;
            const bool wantEq = n.op == Op::Eq || n.op == Op::CaseEq;
            if (a.type == ExprType::Text) {
                const bool eq = exact ? a.text == b.text : equalsNoCase(a.text, b.text);
                r = eq == wantEq ? '1' : '0';
            } else if (exact && a.type == ExprType::Bits && b.type == ExprType::Bits) {
                const std::uint32_t w = static_cast<std::uint32_t>(std::max(a.bits.size(), b.bits.size()));
                bool eq = true;
                for (std::uint32_t i = 0; i < w && eq; ++i)
                    eq = exactBit(extAt(a.bits, w, i, a.isSigned)) ==
                         exactBit(extAt(b.bits, w, i, b.isSigned));
                r = eq == wantEq ? '1' : '0';
            } else if (a.type == ExprType::Real || b.type == ExprType::Real) {
                bool ka = false, kb = false;
                const double x = numberOf(a, ka), y = numberOf(b, kb);
                if (ka && kb) {
                    const int c = x < y ? -1 : x > y ? 1 : 0;
                    r = (n.op == Op::Eq || n.op == Op::CaseEq)   ? (c == 0 ? '1' : '0')
                        : (n.op == Op::Ne || n.op == Op::CaseNe) ? (c != 0 ? '1' : '0')
                        : n.op == Op::Lt                         ? (c < 0 ? '1' : '0')
                        : n.op == Op::Le                         ? (c <= 0 ? '1' : '0')
                        : n.op == Op::Gt                         ? (c > 0 ? '1' : '0')
                                                                 : (c >= 0 ? '1' : '0');
                } else if (exact) {
                    r = (ka == kb && (!ka || x == y)) == wantEq ? '1' : '0';
                }
            } else if (allKnown(a.bits) && allKnown(b.bits)) {
                const int c = compareKnown(a.bits, a.isSigned, b.bits, b.isSigned);
                r = n.op == Op::Eq   ? (c == 0 ? '1' : '0')
                    : n.op == Op::Ne ? (c != 0 ? '1' : '0')
                    : n.op == Op::Lt ? (c < 0 ? '1' : '0')
                    : n.op == Op::Le ? (c <= 0 ? '1' : '0')
                    : n.op == Op::Gt ? (c > 0 ? '1' : '0')
                                     : (c >= 0 ? '1' : '0');
            } else {
                const bool u = a.bits.find('U') != std::string::npos || b.bits.find('U') != std::string::npos;
                r = u ? 'U' : 'X';
            }
            n.bits.assign(1, r);
            break;
        }
        case Op::Add:
        case Op::Sub: {
            const Node& a = kid(0);
            const Node& b = kid(1);
            if (n.type == ExprType::Real) {
                bool ka = false, kb = false;
                const double x = numberOf(a, ka), y = numberOf(b, kb);
                n.real =
                    ka && kb ? (n.op == Op::Add ? x + y : x - y) : std::numeric_limits<double>::quiet_NaN();
            } else {
                addBits(a.bits, a.isSigned, b.bits, b.isSigned, n.op == Op::Sub, n.width, n.bits);
            }
            break;
        }
        case Op::Neg: {
            const Node& a = kid(0);
            if (n.type == ExprType::Real) {
                n.real = -a.real;
            } else {
                static const std::string kZero = "0";
                addBits(kZero, false, a.bits, a.isSigned, true, n.width, n.bits);
            }
            break;
        }
        }
    }
    first_ = false;
    const Node& top = nodes_.back();
    Value v;
    v.bits = &top.bits;
    v.real = top.real;
    v.text = &top.text;
    return v;
}

// ---- ExpressionSignal -------------------------------------------------------------------------------------

ExpressionSignal::ExpressionSignal(Expression expr, std::string name)
    : expr_(std::move(expr))
    , out_(std::move(name), kRootScope, expr_.resultKind(), expr_.width()) {
    const std::size_t n = expr_.inputs().size();
    next_.assign(n, 0);
    index_.assign(n, kNoIndex);
    changed_.assign(n, 0);
}

Result<std::unique_ptr<ExpressionSignal>>
ExpressionSignal::create(std::string_view text, const SignalResolver& resolve, std::string name) {
    using R = Result<std::unique_ptr<ExpressionSignal>>;
    auto e = Expression::compile(text, resolve);
    if (!e.ok())
        return R::failure(e.error());
    std::unique_ptr<ExpressionSignal> s(new ExpressionSignal(std::move(e).value(), std::move(name)));
    s->update();
    return R(std::move(s));
}

void ExpressionSignal::evaluateAt(Time t) {
    const Expression::Value v = expr_.evaluate(index_, changed_);
    switch (expr_.type()) {
    case ExprType::Bits:
        if (!haveLast_ || *v.bits != last_) {
            out_.appendLogic(t, *v.bits);
            last_ = *v.bits;
        }
        break;
    case ExprType::Real: {
        const bool same = haveLast_ && (v.real == lastReal_ || (std::isnan(v.real) && std::isnan(lastReal_)));
        if (!same) {
            out_.appendReal(t, v.real);
            lastReal_ = v.real;
        }
        break;
    }
    case ExprType::Text:
        if (!haveLast_ || *v.text != last_) {
            out_.appendText(t, *v.text);
            last_ = *v.text;
        }
        break;
    }
    haveLast_ = true;
}

std::size_t ExpressionSignal::update() {
    const std::vector<const Signal*>& in = expr_.inputs();
    const std::size_t n = in.size();
    std::uint64_t generation = 0;
    for (const Signal* s : in)
        generation += s->generation();
    if (generation != inputGeneration_) {
        // An input was cleared (a restarted live run): evaluate everything again
        out_.clearChanges();
        std::fill(next_.begin(), next_.end(), 0);
        std::fill(index_.begin(), index_.end(), kNoIndex);
        haveLast_ = false;
        inputGeneration_ = generation;
    }
    const std::size_t before = out_.changeCount();
    while (true) {
        bool any = false;
        Time t = 0;
        for (std::size_t k = 0; k < n; ++k)
            if (next_[k] < in[k]->changeCount()) {
                const Time tk = in[k]->time(next_[k]);
                if (!any || tk < t)
                    t = tk;
                any = true;
            }
        if (!any)
            break;
        for (std::size_t k = 0; k < n; ++k) {
            changed_[k] = 0;
            const Signal& s = *in[k];
            const std::size_t count = s.changeCount();
            while (next_[k] < count && s.time(next_[k]) == t) {
                ++next_[k];
                changed_[k] = 1;
            }
            if (changed_[k] != 0)
                index_[k] = next_[k] - 1;
        }
        evaluateAt(t);
    }
    return out_.changeCount() - before;
}

} // namespace lvd::wave
