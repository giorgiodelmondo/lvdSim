// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvdwave/result.hpp>
#include <lvdwave/store.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace lvd::wave {

/// What one poll() found.
struct PollResult {
    std::size_t valueChanges = 0;        ///< value changes appended to signals
    std::size_t newSignals = 0;          ///< signals (and aliases) defined by this poll
    bool definitionsCompleted = false;   ///< the end-of-definitions marker was read by this poll
    bool timeAdvanced = false;           ///< simulation time moved forward
    std::size_t badLines = 0;            ///< records that could not be understood and were skipped

    bool changed() const noexcept { return valueChanges != 0 || newSignals != 0 || timeAdvanced; }
};

/// Reads the append-only waveform stream that the NVC fork writes with `--format=live` while the
/// simulation is running. Each poll() reads what was appended since the previous one and adds it to
/// the store: the first polls build the scope tree and the signals, later ones append value changes.
///
/// A trailing line that the writer has not finished yet is kept and completed by the next poll.
/// Signals are never added after definitionsComplete(), so references into the store stay valid.
/// The reader is not thread safe: poll() and any use of the store must be on the same thread.
class LiveReader {
public:
    explicit LiveReader(std::string path);

    /// Read new data. Fails if the file cannot be opened (the simulator may not have created it yet)
    /// or the header is wrong. An empty result is not an error.
    Result<PollResult> poll(WaveStore& store);

    const std::string& path() const noexcept { return path_; }
    bool definitionsComplete() const noexcept { return defsDone_; }
    /// Simulation time of the most recent time record.
    Time currentTime() const noexcept { return time_; }
    /// Bytes consumed so far.
    std::uint64_t bytesRead() const noexcept { return offset_; }

private:
    bool apply(const std::string& line, WaveStore& store, PollResult& out);

    std::string path_;
    std::uint64_t offset_ = 0;
    std::string partial_;  ///< incomplete last line of the previous poll
    bool haveHeader_ = false;
    bool defsDone_ = false;
    Time time_ = 0;
    ScopeId scope_ = kRootScope;
    std::vector<SignalId> byHandle_;  ///< stream handle (1-based) to signal; index 0 unused
};

}  // namespace lvd::wave
