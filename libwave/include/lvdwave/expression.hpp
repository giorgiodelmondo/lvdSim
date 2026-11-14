// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvdwave/result.hpp>
#include <lvdwave/store.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace lvd::wave {

/// Expression signals: a wave row computed from other signals ("a and not b", "bus = 16#3F", "cnt > 100",
/// "data(7 downto 4) & flag"). The result is an ordinary Signal, so renderers, the LOD index, the searches
/// and the List window treat it like a recorded one. Syntax reference: docs/expressions.md.
///
/// Summary of the language (case-insensitive keywords, VHDL flavoured with common C style aliases):
/// - names: hierarchical paths as `add wave` takes them (`/tb/dut/q`, `tb.dut.q`, or relative to the design
///   top `q`), optionally with a dataset prefix (`ref:/tb/dut/q`). What a name means is decided by the
///   resolver the caller passes in.
/// - literals: decimal integers (`100`), based (`16#3F#`, `16#3F`, `2#0101#`), bit strings (`x"3F"`,
///   `b"0101"`, `o"17"`, `"0101"`), C and Verilog forms (`0x3F`, `0b101`, `8'h3F`, `4'b10x1`), bits (`'1'`),
///   reals (`1.5`, `2e-3`), `true`, `false`. A string literal compared with an enumeration (text) signal is a
///   text value; so is a bare word that is not a signal (`state = IDLE`).
/// - operators, from the tightest binding: indexing `a(3)`, `a(7 downto 4)`, `a[7:4]`; unary `not ~ ! -`;
///   `+ -` and concatenation `&`; relations `= == /= != < <= > >=` and the exact comparisons `=== !==`;
///   `and && nand`; `xor ^ xnor`; `or || | nor`. `{a, b, c}` concatenates too. Parentheses group.
/// - functions: `signed(x)` and `unsigned(x)` choose how a vector compares and extends.
///
/// Values are nine-valued bit vectors (bit 0 is the least significant), reals or text. Bitwise operators work
/// bit by bit after zero extension to the wider operand. Relations and `+ -` read vectors as unsigned numbers
/// (two's complement after signed()); an unknown bit (X, U, Z, W, -) makes the result X. `===` and `!==`
/// never give X: they compare the values bit for bit (L equals 0, H equals 1). `&&`, `||` and `!` reduce each
/// operand to one bit first (any 1 bit is true).

/// Resolves a name of an expression to a signal. Returns nullptr and fills `error` when the name is unknown.
using SignalResolver = std::function<const Signal*(std::string_view name, std::string& error)>;

enum class ExprType : std::uint8_t { Bits, Real, Text };

/// A parsed and bound expression, ready to evaluate. Not thread safe (evaluation keeps buffers).
class Expression {
public:
    /// Parses and binds. Errors name the column (1 based) and the problem: "col 5: unknown signal 'foo'".
    static Result<Expression> compile(std::string_view text, const SignalResolver& resolve);

    /// Type and width of the result (width is 1 for Real and Text).
    ExprType type() const noexcept { return type_; }
    std::uint32_t width() const noexcept { return width_; }
    SignalKind resultKind() const noexcept;
    /// "1 bit", "8 bit vector", "real", "text".
    std::string describe() const;
    /// The distinct signals the expression reads, in order of first use.
    const std::vector<const Signal*>& inputs() const noexcept { return inputs_; }
    const std::string& text() const noexcept { return text_; }

    /// Value of the expression when input k holds change `index[k]` (kNoIndex: no value yet, read as U).
    /// `changed[k]` says whether input k differs from the previous call (all true the first time); untouched
    /// sub-expressions are not recomputed. Bits results are MSB first; Real results are in `real`.
    struct Value {
        const std::string* bits = nullptr;
        double real = 0.0;
        const std::string* text = nullptr;
    };
    Value evaluate(const std::vector<std::size_t>& index, const std::vector<char>& changed);

    struct Node; ///< implementation detail, public for the compiler helpers in expression.cpp

    Expression();
    ~Expression();
    Expression(Expression&&) noexcept;
    Expression& operator=(Expression&&) noexcept;

private:
    std::string text_;
    ExprType type_ = ExprType::Bits;
    std::uint32_t width_ = 1;
    std::vector<const Signal*> inputs_;
    std::vector<Node> nodes_; ///< post order: children before parents, the result last
    bool first_ = true;
    friend class ExprCompiler;
};

/// An expression evaluated over time into a Signal. update() reads the input changes added since the previous
/// call (all of them the first time) and appends the resulting changes, so a live run is followed
/// incrementally. One output change per time step at which an input changes and the result differs; changes
/// of several inputs at one time count as one step. If more delta cycles arrive later for the last time step
/// already evaluated, the value of that step is appended again (the last change at a time wins, as in the
/// store). Cost: O(input changes x expression size x width) for all calls together; nothing is re-evaluated.
///
/// The input signals must outlive this object and may only grow (append) between calls, or be cleared
/// (Signal::clearChanges, a restarted live run): then the next update() evaluates everything again.
class ExpressionSignal {
public:
    static Result<std::unique_ptr<ExpressionSignal>>
    create(std::string_view text, const SignalResolver& resolve, std::string name = "expr");

    /// Processes new input changes; returns the number of changes appended to signal().
    std::size_t update();
    const Signal& signal() const noexcept { return out_; }
    const Expression& expression() const noexcept { return expr_; }

private:
    ExpressionSignal(Expression expr, std::string name);
    void evaluateAt(Time t);

    Expression expr_;
    Signal out_;
    std::vector<std::size_t> next_;  ///< per input: index of the next unread change
    std::vector<std::size_t> index_; ///< per input: change in effect (kNoIndex if none)
    std::vector<char> changed_;
    std::string last_; ///< last appended value (bits or text), for change detection
    double lastReal_ = 0.0;
    bool haveLast_ = false;
    std::uint64_t inputGeneration_ = 0; ///< sum of the inputs' Signal::generation() at the last update
};

} // namespace lvd::wave
