// SPDX-License-Identifier: GPL-3.0-or-later
#include <lvdwave/store.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

namespace lvd::wave {

Signal::Signal(std::string name, ScopeId scope, SignalKind kind, std::uint32_t width, SignalId aliasOf)
    : name_(std::move(name))
    , scope_(scope)
    , kind_(kind)
    , width_(kind == SignalKind::Logic ? std::max<std::uint32_t>(width, 1) : 1)
    , aliasOf_(aliasOf) {}

std::size_t Signal::holdIndex(Time t) const noexcept {
    auto it = std::upper_bound(times_.begin(), times_.end(), t);
    if (it == times_.begin())
        return kNoIndex;
    return static_cast<std::size_t>(it - times_.begin()) - 1;
}

std::pair<std::size_t, std::size_t> Signal::changesIn(Time t0, Time t1) const noexcept {
    auto lo = std::lower_bound(times_.begin(), times_.end(), t0);
    auto hi = std::lower_bound(lo, times_.end(), t1);
    return {static_cast<std::size_t>(lo - times_.begin()), static_cast<std::size_t>(hi - times_.begin())};
}

Logic Signal::bit(std::size_t change, std::uint32_t bitIndex) const noexcept {
    if (width_ == 1)
        return static_cast<Logic>(scalar_[change]);
    const std::size_t base = change * bytesPerChange();
    const std::uint8_t byte = packed_[base + bitIndex / 2];
    return static_cast<Logic>((bitIndex & 1u) ? (byte >> 4) : (byte & 0xFu));
}

std::string Signal::bits(std::size_t change) const {
    std::string s(width_, '0');
    for (std::uint32_t i = 0; i < width_; ++i)
        s[width_ - 1 - i] = toChar(bit(change, i));
    return s;
}

bool Signal::sameValue(std::size_t a, std::size_t b) const noexcept {
    switch (kind_) {
    case SignalKind::Real:
        return reals_[a] == reals_[b] || (std::isnan(reals_[a]) && std::isnan(reals_[b]));
    case SignalKind::String:
        return textIds_[a] == textIds_[b];
    case SignalKind::Logic:
        break;
    }
    if (width_ == 1)
        return scalar_[a] == scalar_[b];
    const std::size_t n = bytesPerChange();
    return std::memcmp(&packed_[a * n], &packed_[b * n], n) == 0;
}

void Signal::setNibble(std::size_t byteBase, std::uint32_t bitIndex, Logic v) noexcept {
    std::uint8_t& byte = packed_[byteBase + bitIndex / 2];
    const auto code = static_cast<std::uint8_t>(v);
    byte = (bitIndex & 1u) ? static_cast<std::uint8_t>((byte & 0x0Fu) | (code << 4))
                           : static_cast<std::uint8_t>((byte & 0xF0u) | code);
}

void Signal::appendLogic(Time t, std::string_view msbFirst) {
    assert(kind_ == SignalKind::Logic);
    assert(times_.empty() || t >= times_.back());
    times_.push_back(t);

    if (width_ == 1) {
        scalar_.push_back(static_cast<std::uint8_t>(msbFirst.empty() ? Logic::X : fromChar(msbFirst.back())));
        return;
    }

    const std::size_t base = packed_.size();
    packed_.resize(base + bytesPerChange(), 0);

    // Left extension: zero unless the leading character is x/z/u.
    Logic pad = Logic::L0;
    if (!msbFirst.empty()) {
        const Logic lead = fromChar(msbFirst.front());
        if (lead == Logic::X || lead == Logic::Z || lead == Logic::U)
            pad = lead;
    }

    const std::size_t n = msbFirst.size();
    for (std::uint32_t i = 0; i < width_; ++i) {
        // Bit i counted from the least significant end of the supplied string.
        const Logic v = i < n ? fromChar(msbFirst[n - 1 - i]) : pad;
        setNibble(base, i, v);
    }
}

void Signal::appendReal(Time t, double v) {
    assert(kind_ == SignalKind::Real);
    assert(times_.empty() || t >= times_.back());
    times_.push_back(t);
    reals_.push_back(v);
}

void Signal::appendText(Time t, std::string_view s) {
    assert(kind_ == SignalKind::String);
    assert(times_.empty() || t >= times_.back());
    times_.push_back(t);

    std::string key(s);
    auto it = tableIndex_.find(key);
    if (it == tableIndex_.end()) {
        it = tableIndex_.emplace(key, static_cast<std::uint32_t>(table_.size())).first;
        table_.push_back(std::move(key));
    }
    textIds_.push_back(it->second);
}

std::vector<std::size_t> Signal::bitChanges(std::uint32_t bitIndex) const {
    std::vector<std::size_t> out;
    if (kind_ != SignalKind::Logic || bitIndex >= width_)
        return out;
    Logic prev = Logic::L0;
    for (std::size_t i = 0; i < times_.size(); ++i) {
        const Logic v = bit(i, bitIndex);
        if (i == 0 || v != prev)
            out.push_back(i);
        prev = v;
    }
    return out;
}

std::size_t Signal::memoryBytes() const noexcept {
    return times_.capacity() * sizeof(Time) + scalar_.capacity() + packed_.capacity() +
           reals_.capacity() * sizeof(double) + textIds_.capacity() * sizeof(std::uint32_t);
}

// ---------------------------------------------------------------------------------------------

WaveStore::WaveStore() {
    scopes_.push_back(Scope{});
}

ScopeId WaveStore::addScope(ScopeId parent, std::string name, int type) {
    const auto id = static_cast<ScopeId>(scopes_.size());
    Scope s;
    s.name = std::move(name);
    s.parent = parent;
    s.type = type;
    scopes_.push_back(std::move(s));
    scopes_[parent].children.push_back(id);
    pathIndexValid_ = false;
    scopeIndexValid_ = false;
    return id;
}

SignalId WaveStore::addSignal(ScopeId scope, std::string name, SignalKind kind, std::uint32_t width) {
    const auto id = static_cast<SignalId>(signals_.size());
    signals_.emplace_back(std::move(name), scope, kind, width);
    scopes_[scope].signalIds.push_back(id);
    pathIndexValid_ = false;
    return id;
}

SignalId WaveStore::addAlias(ScopeId scope, std::string name, SignalId target) {
    const SignalId real = resolve(target);
    const Signal& t = signals_[real];
    const auto id = static_cast<SignalId>(signals_.size());
    signals_.emplace_back(std::move(name), scope, t.kind(), t.width(), real);
    scopes_[scope].signalIds.push_back(id);
    pathIndexValid_ = false;
    return id;
}

SignalId WaveStore::resolve(SignalId id) const noexcept {
    const SignalId a = signals_[id].aliasOf();
    return a == kNoSignal ? id : a;
}

const Signal& WaveStore::signal(SignalId id) const {
    return signals_[resolve(id)];
}
Signal& WaveStore::mutableSignal(SignalId id) {
    return signals_[resolve(id)];
}

std::string WaveStore::scopePath(ScopeId id) const {
    std::vector<const std::string*> parts;
    for (ScopeId s = id; s != kRootScope; s = scopes_[s].parent)
        parts.push_back(&scopes_[s].name);
    std::string out;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        if (!out.empty())
            out += '.';
        out += **it;
    }
    return out;
}

std::string WaveStore::pathOf(SignalId id) const {
    std::string p = scopePath(signals_[id].scope());
    if (!p.empty())
        p += '.';
    p += signals_[id].name();
    return p;
}

SignalId WaveStore::findSignal(std::string_view path) const {
    if (!pathIndexValid_) {
        pathIndex_.clear();
        pathIndex_.reserve(signals_.size());
        for (SignalId i = 0; i < signals_.size(); ++i)
            pathIndex_.emplace(pathOf(i), i);
        pathIndexValid_ = true;
    }
    auto it = pathIndex_.find(std::string(path));
    return it == pathIndex_.end() ? kNoSignal : it->second;
}

ScopeId WaveStore::findScope(std::string_view path) const {
    if (!scopeIndexValid_) {
        scopeIndex_.clear();
        scopeIndex_.reserve(scopes_.size());
        for (ScopeId i = 0; i < scopes_.size(); ++i)
            scopeIndex_.emplace(scopePath(i), i);
        scopeIndexValid_ = true;
    }
    auto it = scopeIndex_.find(std::string(path));
    return it == scopeIndex_.end() ? kNoScope : it->second;
}

void Signal::clearChanges() {
    times_.clear();
    scalar_.clear();
    packed_.clear();
    reals_.clear();
    textIds_.clear();
    table_.clear();
    tableIndex_.clear();
    ++generation_;
}

void WaveStore::clearValues() {
    for (Signal& s : signals_)
        s.clearChanges();
    start_ = end_ = 0;
    haveTime_ = false;
}

void WaveStore::extendTimeRange(Time t) noexcept {
    if (!haveTime_) {
        start_ = end_ = t;
        haveTime_ = true;
        return;
    }
    start_ = std::min(start_, t);
    end_ = std::max(end_, t);
}

std::uint64_t WaveStore::totalChanges() const noexcept {
    std::uint64_t n = 0;
    for (const Signal& s : signals_)
        if (s.aliasOf() == kNoSignal)
            n += s.changeCount();
    return n;
}

std::size_t WaveStore::memoryBytes() const noexcept {
    std::size_t n = 0;
    for (const Signal& s : signals_)
        n += s.memoryBytes();
    return n;
}

} // namespace lvd::wave
