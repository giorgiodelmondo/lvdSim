// SPDX-License-Identifier: GPL-3.0-or-later
#include <lvdwave/live_reader.hpp>

#include <cctype>
#include <cstdlib>
#include <fstream>

namespace lvd::wave {

namespace {

// Splits off the next space separated word. Returns false if there is none.
bool nextWord(const std::string& s, std::size_t& pos, std::string& word) {
    while (pos < s.size() && s[pos] == ' ') ++pos;
    if (pos >= s.size()) return false;
    const std::size_t start = pos;
    while (pos < s.size() && s[pos] != ' ') ++pos;
    word = s.substr(start, pos - start);
    return true;
}

std::string restOfLine(const std::string& s, std::size_t pos) {
    while (pos < s.size() && s[pos] == ' ') ++pos;
    return s.substr(pos);
}

bool parseUnsigned(const std::string& w, std::uint64_t& v) {
    if (w.empty()) return false;
    char* end = nullptr;
    v = std::strtoull(w.c_str(), &end, 10);
    return end != nullptr && *end == '\0';
}

std::string unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            ++i;
            out += s[i] == 'n' ? '\n' : s[i] == 'r' ? '\r' : s[i];
        } else {
            out += s[i];
        }
    }
    return out;
}

PortDirection directionFromWord(const std::string& w) {
    if (w == "in") return PortDirection::In;
    if (w == "out") return PortDirection::Out;
    if (w == "inout") return PortDirection::InOut;
    if (w == "buffer") return PortDirection::Buffer;
    if (w == "linkage") return PortDirection::Linkage;
    if (w == "internal") return PortDirection::Internal;
    return PortDirection::Unknown;
}

}  // namespace

LiveReader::LiveReader(std::string path) : path_(std::move(path)), byHandle_(1, kNoSignal) {}

Result<PollResult> LiveReader::poll(WaveStore& store) {
    std::ifstream f(path_, std::ios::binary);
    if (!f) return Result<PollResult>::failure("cannot open live waveform stream: " + path_);

    f.seekg(0, std::ios::end);
    const auto size = static_cast<std::uint64_t>(f.tellg());
    PollResult out;
    if (size < offset_) return Result<PollResult>::failure("live waveform stream was truncated: " + path_);
    if (size == offset_) return out;

    std::string chunk(static_cast<std::size_t>(size - offset_), '\0');
    f.seekg(static_cast<std::streamoff>(offset_));
    f.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    chunk.resize(static_cast<std::size_t>(f.gcount()));
    offset_ += chunk.size();

    partial_ += chunk;
    std::size_t start = 0;
    for (;;) {
        const std::size_t nl = partial_.find('\n', start);
        if (nl == std::string::npos) break;
        std::string line = partial_.substr(start, nl - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        start = nl + 1;

        if (!haveHeader_) {
            if (line != "LVW 1") return Result<PollResult>::failure("not a live waveform stream (bad header): " + path_);
            haveHeader_ = true;
            continue;
        }
        if (!line.empty() && !apply(line, store, out)) ++out.badLines;
    }
    partial_.erase(0, start);
    return out;
}

bool LiveReader::apply(const std::string& line, WaveStore& store, PollResult& out) {
    if (line[0] == '#') {
        std::uint64_t t = 0;
        if (!parseUnsigned(line.substr(1), t)) return false;
        if (t > time_) out.timeAdvanced = true;
        time_ = t;
        store.extendTimeRange(t);
        return true;
    }

    std::size_t pos = 0;
    std::string w;
    if (!nextWord(line, pos, w)) return false;

    // Value record: <handle> <kind> <value>
    if (std::isdigit(static_cast<unsigned char>(w[0]))) {
        std::uint64_t h = 0;
        std::string kind;
        if (!parseUnsigned(w, h) || h <= handleBase_) return false;
        h -= handleBase_;  // handles of a restarted run are offset by the base of the last R record
        if (h >= byHandle_.size() || !nextWord(line, pos, kind) || kind.size() != 1) return false;
        if (byHandle_[h] == kNoSignal) return false;
        Signal& s = store.mutableSignal(byHandle_[h]);
        const std::string value = restOfLine(line, pos);
        // A time that goes backwards would break the store; treat it as a bad record.
        if (s.changeCount() > 0 && time_ < s.time(s.changeCount() - 1)) return false;
        switch (kind[0]) {
            case 'L':
                if (s.kind() != SignalKind::Logic) return false;
                s.appendLogic(time_, value);
                break;
            case 'R':
                if (s.kind() != SignalKind::Real) return false;
                s.appendReal(time_, std::strtod(value.c_str(), nullptr));
                break;
            case 'S':
                if (s.kind() != SignalKind::String) return false;
                s.appendText(time_, unescape(value));
                break;
            default:
                return false;
        }
        ++out.valueChanges;
        return true;
    }

    if (w == "R") {
        // Restart: time goes back to zero and the values so far belong to the earlier run. The definitions stay;
        // the base grows by the number of handles at every restart, which is checked so that a stream this reader
        // does not understand cannot put values on the wrong signals.
        std::string bs;
        std::uint64_t base = 0;
        if (!defsDone_ || !nextWord(line, pos, bs) || !parseUnsigned(bs, base) ||
            base != handleBase_ + (byHandle_.size() - 1))
            return false;
        handleBase_ = base;
        store.clearValues();
        time_ = 0;
        out.restarted = true;
        ++restarts_;
        return true;
    }
    if (w == "E") {
        if (!defsDone_) out.definitionsCompleted = true;
        defsDone_ = true;
        return true;
    }
    if (w == "TS") {
        std::string e;
        if (!nextWord(line, pos, e)) return false;
        store.setTimescaleExponent(std::atoi(e.c_str()));
        return true;
    }
    if (w == "U") {
        if (scope_ != kRootScope) scope_ = store.scope(scope_).parent;
        return true;
    }
    if (w == "S") {
        std::string type;
        if (!nextWord(line, pos, type)) return false;
        scope_ = store.addScope(scope_, restOfLine(line, pos), std::atoi(type.c_str()));
        return true;
    }
    if (w == "V") {
        std::string hs, ts, ws, kind;
        std::uint64_t h = 0, width = 0;
        if (!nextWord(line, pos, hs) || !nextWord(line, pos, ts) || !nextWord(line, pos, ws) || !nextWord(line, pos, kind) ||
            !parseUnsigned(hs, h) || !parseUnsigned(ws, width) || kind.size() != 1 || h == 0)
            return false;
        std::string name = restOfLine(line, pos);
        if (defsDone_) return false;  // keeps references into the store stable

        if (h < byHandle_.size() && byHandle_[h] != kNoSignal) {
            lastEntry_ = store.addAlias(scope_, std::move(name), byHandle_[h]);
        } else {
            const SignalKind k = kind[0] == 'R' ? SignalKind::Real : kind[0] == 'S' ? SignalKind::String : SignalKind::Logic;
            const SignalId id = store.addSignal(scope_, std::move(name), k, static_cast<std::uint32_t>(width));
            if (h >= byHandle_.size()) byHandle_.resize(h + 1, kNoSignal);
            byHandle_[h] = id;
            lastEntry_ = id;
        }
        ++out.newSignals;
        return true;
    }
    if (w == "D") {
        // Port direction of the entry of the preceding V record. A word this reader does not know leaves it Unknown.
        std::string dir;
        if (lastEntry_ == kNoSignal || defsDone_ || !nextWord(line, pos, dir)) return false;
        store.setDirection(lastEntry_, directionFromWord(dir));
        lastEntry_ = kNoSignal;
        return true;
    }
    return false;
}

}  // namespace lvd::wave
