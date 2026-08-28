#pragma once

#include "TimeTagger.h"
#include "TimeTagStreamBuffer.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <random>
#include <vector>

// Fake of TimeTagStream: lazily generates events for each watched channel
// on every getData() call, based on wall-clock time elapsed since
// construction (or the previous getData() call). Uses whatever
// ChannelPatternRegistry (TimeTagger.h) pattern is registered for a
// channel, if any -- e.g. from measurements/SignalGenerators.h's
// PatternSignalGenerator -- then falls back to TimeTagger.h's hardcoded
// kSimulatedLineClockChannel/kSimulatedSyncChannel (fixed, always-periodic
// channels standing in for a scanner's line clock and laser sync), then
// kSimulatedPhotonChannel's gated Poisson noise correlated to the
// simulated sync channel (see that constant's comment in TimeTagger.h),
// and only after all of that to plain Poisson arrivals at the fixed
// kDefaultChannelRateHz (see TimeTagger.h -- there's no API on this fake
// for configuring a channel's simulated rate directly, matching the real
// TimeTaggerBase, which has no such API either). Matches real hardware
// semantics: capture starts at construction, so the first getData() call
// already returns real events.
class TimeTagStream {
  public:
    TimeTagStream(TimeTaggerBase * /*tagger*/, std::uint64_t /*n_max_events*/,
                  std::vector<channel_t> channels)
        : last_call_(std::chrono::steady_clock::now()) {
        for (channel_t const channel : channels) {
            if (channel == kSimulatedPhotonChannel)
                photon_pos_registered_ = true;
            if (channel == -kSimulatedPhotonChannel)
                photon_neg_registered_ = true;

            ChannelState state;
            state.channel = channel;
            state.pattern = ChannelPatternRegistry::Find(channel);
            if (!state.pattern && channel == kSimulatedLineClockChannel) {
                // Hardcoded simulated line clock (see TimeTagger.h's
                // kSimulatedLineClockChannel) -- only when nothing has
                // explicitly registered a pattern for this channel, so an
                // explicit PatternSignalGenerator still wins.
                state.pattern = ChannelPatternRegistry::Pattern{
                    {0}, true, 0, kSimulatedLinePeriodPs};
            }
            if (!state.pattern && channel == kSimulatedSyncChannel) {
                state.pattern = ChannelPatternRegistry::Pattern{
                    {0}, true, 0, kSimulatedPixelPeriodPs};
            }
            if (!state.pattern && channel == -kSimulatedSyncChannel) {
                state.pattern = ChannelPatternRegistry::Pattern{
                    {kSimulatedSyncPulseWidth}, true, 0,
                    kSimulatedPixelPeriodPs};
            }
            // kSimulatedPhotonChannel/-kSimulatedPhotonChannel (without an
            // explicit pattern) are handled separately in getData(), as a
            // correlated pair, so they deliberately get neither a pattern
            // nor a Poisson state here.
            bool const is_gated_photon =
                !state.pattern &&
                (channel == kSimulatedPhotonChannel ||
                 channel == -kSimulatedPhotonChannel);
            if (!state.pattern && !is_gated_photon) {
                state.interval_dist =
                    std::exponential_distribution<double>(kDefaultChannelRateHz);
                state.next_arrival_ps = state.interval_dist(rng_) * 1e12;
            }
            state.is_gated_photon = is_gated_photon;
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

        if (photon_pos_registered_ || photon_neg_registered_) {
            // Gated Poisson photon noise correlated to the simulated sync
            // channel (see kSimulatedPhotonChannel's comment in
            // TimeTagger.h). Both polarities are generated together here,
            // from one shared draw sequence, so pair_one_between finds a
            // matching rising/falling pair for every simulated photon
            // instead of two independently drifting tag streams.
            std::exponential_distribution<double> gate_dist(
                kSimulatedPhotonGateRateHz);
            if (!next_photon_candidate_ps_)
                next_photon_candidate_ps_ = gate_dist(rng_) * 1e12;
            for (;;) {
                double &t = *next_photon_candidate_ps_;
                if (t >= elapsed_ps_)
                    break;
                auto const period_index = static_cast<std::int64_t>(
                    t / static_cast<double>(kSimulatedPixelPeriodPs));
                double const period_start =
                    static_cast<double>(period_index) *
                    static_cast<double>(kSimulatedPixelPeriodPs);
                double const offset_in_period = t - period_start;
                if (offset_in_period <
                    static_cast<double>(kSimulatedPhotonGateWidthPs)) {
                    if (photon_pos_registered_ &&
                        !ChannelPatternRegistry::Find(kSimulatedPhotonChannel)) {
                        buffer.timestamps_.push_back(
                            static_cast<timestamp_t>(t));
                        buffer.channels_.push_back(kSimulatedPhotonChannel);
                    }
                    if (photon_neg_registered_ &&
                        !ChannelPatternRegistry::Find(
                            -kSimulatedPhotonChannel)) {
                        buffer.timestamps_.push_back(static_cast<timestamp_t>(
                            t + kSimulatedPhotonPulseWidth));
                        buffer.channels_.push_back(-kSimulatedPhotonChannel);
                    }
                    t += gate_dist(rng_) * 1e12;
                } else {
                    // Past this period's gate -- jump straight to the start
                    // of the next one instead of continuing to draw
                    // candidates that would only land in dead time.
                    t = period_start +
                        static_cast<double>(kSimulatedPixelPeriodPs);
                }
            }
        }

        for (auto &state : channels_) {
            if (state.is_gated_photon)
                continue; // handled above
            if (state.pattern) {
                auto const &pattern = *state.pattern;
                if (pattern.sequence.empty())
                    continue;
                std::size_t const cycle_len = pattern.sequence.size();
                for (;;) {
                    std::size_t const cycle =
                        state.pattern_next_index / cycle_len;
                    if (!pattern.repeat && cycle > 0)
                        break; // one-shot pattern already fully emitted
                    std::size_t const offset_index =
                        state.pattern_next_index % cycle_len;
                    double const t =
                        static_cast<double>(pattern.start_delay) +
                        static_cast<double>(cycle) *
                            static_cast<double>(pattern.spacing) +
                        static_cast<double>(pattern.sequence[offset_index]);
                    if (t >= elapsed_ps_)
                        break;
                    buffer.timestamps_.push_back(static_cast<timestamp_t>(t));
                    buffer.channels_.push_back(state.channel);
                    ++state.pattern_next_index;
                }
                continue;
            }

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
        std::optional<ChannelPatternRegistry::Pattern> pattern;
        std::size_t pattern_next_index = 0;
        std::exponential_distribution<double> interval_dist;
        double next_arrival_ps = 0.0; // next scheduled (not yet emitted) arrival
        bool is_gated_photon = false; // handled in getData(), not here
    };

    std::mt19937_64 rng_{std::random_device{}()};
    std::vector<ChannelState> channels_;
    double elapsed_ps_ = 0.0; // total simulated time elapsed so far
    std::chrono::steady_clock::time_point last_call_;

    bool photon_pos_registered_ = false;
    bool photon_neg_registered_ = false;
    std::optional<double> next_photon_candidate_ps_;
};
