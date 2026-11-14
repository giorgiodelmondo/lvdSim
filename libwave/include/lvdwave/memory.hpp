// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvdwave/radix.hpp>
#include <lvdwave/result.hpp>
#include <lvdwave/search.hpp>
#include <lvdwave/store.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lvd::wave {

/// Memories (arrays of words) as the waveform carries them.
///
/// NVC writes an array of vectors or integers as one signal per element when the dump includes arrays
/// (`--dump-arrays`): `mem[0][7:0]`, `mem[1][7:0]`, ... for `array (0 to N) of std_logic_vector(7 downto 0)`,
/// `imem[3]`, `imem[2]`, ... for an array of integers. An array of arrays of vectors gives
/// `m[0][1][3:0]`: every row of the outer array is a memory of its own (`m[0]`). A memory is a group of Logic
/// signals of one scope with the same width whose names are `<base>[<integer>]`, optionally followed by one
/// range group `[msb:lsb]`.
struct MemoryInfo {
    ScopeId scope = kRootScope;
    std::string name;            ///< "mem" (with the outer indexes of a nested array: "m[0]")
    std::string path;            ///< dotted store path: "top.ram.mem"
    std::string elementRange;    ///< "[7:0]" when the elements are vectors, else empty
    std::int64_t left = 0;       ///< index of the first element in declaration (dump) order
    std::int64_t right = 0;      ///< index of the last element in declaration order
    std::int64_t low = 0;        ///< lowest index: words[i] holds index low + i
    std::vector<SignalId> words; ///< store entries by index; kNoSignal where an index is missing
    std::uint32_t width = 1;     ///< bits per word
    std::size_t depth() const noexcept { return words.size(); }
    std::int64_t high() const noexcept { return low + static_cast<std::int64_t>(words.size()) - 1; }
};

/// Every memory of the store with at least minWords elements, sorted by path. O(signals).
std::vector<MemoryInfo> findMemories(const WaveStore& store, std::size_t minWords = 2);
/// One memory by path: dotted ("top.ram.mem") or as `add wave` writes it ("/top/ram/mem"). Case-insensitive.
std::optional<MemoryInfo> findMemory(const WaveStore& store, std::string_view path);
/// Splits an element name: "mem[12][7:0]" -> base "mem", index 12, range "[7:0]". False if the name is not
/// an element name.
bool parseElementName(std::string_view name, std::string& base, std::int64_t& index, std::string& range);

/// The contents of one memory at any recorded time, built from the element signals of the store.
///
/// The element signals are indexed once: for every word the changes at which its value really changes (a
/// producer that repeats unchanged values, as NVC does for every element when one element of an array
/// changes, costs nothing later), and one time-ordered list of all writes. The first value of a word is its
/// initial value, not a write. update() adds what a live source appended since the last call; it only looks
/// at the new changes.
///
/// Costs (W words, n changes of one word, w writes in total):
/// - value of a word at t: O(log n) (the store's binary search); a screen of words is a few thousand of
/// those.
/// - lastWrite(word, t): O(log writes of that word).
/// - writes at a time, previous / next write time: O(log w + k).
/// - findWord: O(W log n) in the worst case.
/// - update(): O(W + new changes + new writes * log) ; the index holds 12 bytes per write.
/// The store must outlive the image and must not lose signals (a live store only grows).
class MemoryImage {
public:
    struct Write {
        Time time = 0;
        std::uint32_t word = 0; ///< word number (index - low)
    };

    MemoryImage(const WaveStore& store, MemoryInfo info);

    const MemoryInfo& info() const noexcept { return info_; }
    std::size_t depth() const noexcept { return info_.words.size(); }

    /// Reads the changes appended to the element signals since the last call. True if there were new writes, or
    /// if the element signals were cleared (Signal::clearChanges, a restarted live run) and the writes start again.
    bool update();

    /// The element signal of a word (alias resolved), or nullptr for a missing index.
    const Signal* wordSignal(std::size_t word) const noexcept;
    /// Change of the word's signal in effect at t, or kNoIndex (before its first value, or a missing index).
    std::size_t changeAt(std::size_t word, Time t) const noexcept;
    /// The word at t, most significant bit first; empty when it has no value yet.
    std::string bits(std::size_t word, Time t) const;
    /// The word at t in a radix; "-" when it has no value yet.
    std::string format(std::size_t word, Time t, Radix radix) const;

    /// Time of the last write to the word at or before t, if any.
    std::optional<Time> lastWrite(std::size_t word, Time t) const noexcept;
    /// Number of writes to the word at or before t.
    std::size_t writeCount(std::size_t word, Time t) const noexcept;

    /// Every write in time order (equal times by word). Several writes of one word in one time step (delta
    /// cycles) are listed once per change.
    const std::vector<Write>& writes() const noexcept { return writes_; }
    /// The words written at exactly t, ascending, each once.
    std::vector<std::size_t> wordsWrittenAt(Time t) const;
    /// The time of the latest write at or before t.
    std::optional<Time> lastWriteTime(Time t) const noexcept;
    /// The first write time strictly after t / the last one strictly before t.
    std::optional<Time> nextWriteTime(Time t) const noexcept;
    std::optional<Time> previousWriteTime(Time t) const noexcept;

    /// The next word (Forward: after `from`, Backward: before it) whose value at t matches. `from` = nullopt
    /// starts at the first (last) word. With wrap the search continues at the other end. The pattern must be
    /// built for this memory's width (ValuePattern::parse(text, radix, SignalKind::Logic, info().width)).
    std::optional<std::size_t> findWord(const ValuePattern& pattern, Time t, std::optional<std::size_t> from,
                                        SearchDirection dir, bool wrap = false) const;

    /// The memory at t as text that $readmemh (Hex) or $readmemb (Binary) reads: a comment header, an
    /// `@address` line (hexadecimal) and the words, wordsPerLine to a line, lowest address first. Unknown
    /// digits are written as x (z when the digit is all Z); a word without a value is all x.
    std::string memoryFileText(Time t, Radix radix, unsigned wordsPerLine = 16,
                               std::string_view timeText = {}) const;

private:
    const WaveStore& store_;
    MemoryInfo info_;
    std::vector<const Signal*> signals_;
    std::vector<std::size_t> scanned_;                ///< changes of each word already looked at
    std::vector<std::vector<std::uint32_t>> changes_; ///< per word: changes that are writes
    std::vector<Write> writes_;
    std::uint64_t generation_ = 0; ///< sum of the element signals' Signal::generation() at the last update
};

/// `add mem [-addressradix r] [-dataradix r] [-wordsperline n] path...` (words after "add mem").
struct AddMemRequest {
    std::vector<std::string> paths;
    std::optional<Radix> addressRadix;
    std::optional<Radix> dataRadix;
    std::optional<unsigned> wordsPerLine;
};
Result<AddMemRequest> parseAddMem(const std::vector<std::string>& words);

/// An address in a radix: Hex, Octal, Binary (digits, no prefix, at least `digits` long), Unsigned or Signed
/// (decimal). Used by the memory window for the address column.
std::string formatAddress(std::int64_t address, Radix radix, int digits = 0);
/// Parses an address typed by the user: decimal, 0x / 16#..# / x"..", 0b / 2#..#, 0o / 8#..#, or digits in
/// `radix` when there is no prefix. Underscores are ignored. nullopt when it is not a number.
std::optional<std::int64_t> parseAddress(std::string_view text, Radix radix);

} // namespace lvd::wave
