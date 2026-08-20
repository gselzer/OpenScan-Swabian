#pragma once

#include "TimeTagger.h"
#include "TimeTagStreamBuffer.h"

#include <chrono>
#include <cstdint>
#include <random>
#include <vector>

// Fake of TimeTagStream: lazily generates Poisson-arrival events for each
// watched channel on every getData() call, based on wall-clock time
// elapsed since construction (or the previous getData() call), at
// whatever rate the channel was registered with (see
// TimeTaggerBase::RegisterChannel / the fake Experimental signal
// generators). Matches real hardware semantics: capture starts at
// construction, so the first getData() call already returns real events.
class TimeTagStream {
  public:
    TimeTagStream(TimeTaggerBase *tagger, std::uint64_t /*n_max_events*/,
                  std::vector<channel_t> channels)
        : last_call_(std::chrono::steady_clock::now()) {
        for (channel_t const channel : channels) {
            ChannelState state;
            state.channel = channel;
            state.interval_dist = std::exponential_distribution<double>(
                tagger->GetChannelRate(channel));
            state.next_arrival_ps = state.interval_dist(rng_) * 1e12;
            channels_.push_back(std::move(state));
        }
    }

    TimeTagStreamBuffer getData() {
        TimeTagStreamBuffer buffer;

        auto const now = std::chrono::steady_clock::now();
        auto const elapsed_s =
            std::chrono::duration_cast<std::chrono::duration<double>>(
                now - last_call_)
                .count();
        last_call_ = now;
        elapsed_ps_ += elapsed_s * 1e12;

        for (auto &state : channels_) {
            while (state.next_arrival_ps < elapsed_ps_) {
                buffer.timestamps_.push_back(
                    static_cast<timestamp_t>(state.next_arrival_ps));
                buffer.channels_.push_back(state.channel);
                state.next_arrival_ps += state.interval_dist(rng_) * 1e12;
            }
        }
        buffer.size = buffer.timestamps_.size();
        return buffer;
    }

  private:
    struct ChannelState {
        channel_t channel = 0;
        std::exponential_distribution<double> interval_dist;
        double next_arrival_ps = 0.0; // next scheduled (not yet emitted) arrival
    };

    std::mt19937_64 rng_{std::random_device{}()};
    std::vector<ChannelState> channels_;
    double elapsed_ps_ = 0.0; // total simulated time elapsed so far
    std::chrono::steady_clock::time_point last_call_;
};
