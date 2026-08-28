#pragma once

#include "TimeTagger.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace Experimental {

namespace internal {

// Fake-only: a self-contained channel-number allocator, entirely separate
// from TimeTaggerBase (which has no such API, matching the real SDK).
// Stands in for what IteratorBase::getNewVirtualChannel() would give a
// signal generator on real hardware. Shared by every generator in this
// file so two generators both constructed with base_channel=CHANNEL_UNUSED
// never collide with each other. Starts at 1000, matching the real SDK's
// own documented behavior ("Virtual channels will automatically obtain
// numbers higher than the positive channel numbers" -- TimeTagger.h) --
// also keeps auto-allocated channels well clear of TimeTagger.h's
// kSimulatedLineClockChannel/kSimulatedSyncChannel/kSimulatedPhotonChannel
// (1-3), which would otherwise silently collide with the first few
// auto-allocated channels and get reinterpreted as one of those roles.
inline channel_t AllocateVirtualChannel() {
    static channel_t next_channel = 1000;
    return next_channel++;
}

} // namespace internal

// Fake of Experimental::ExponentialSignalGenerator. Produces
// Poisson-arrival events on `base_channel` (or a freshly allocated
// channel if base_channel is CHANNEL_UNUSED, matching real semantics) --
// actual event generation happens lazily in TimeTagStream::getData(),
// always at the fixed TimeTagger.h::kDefaultChannelRateHz.
//
// NOTE: `rate` is accepted for signature compatibility with the real
// class but is NOT applied -- there is no API on this fake for
// configuring a channel's simulated rate (see TimeTagger.h's file
// comment). Every simulated channel runs at kDefaultChannelRateHz
// regardless of what's passed here. For a deterministic (non-random)
// channel, use PatternSignalGenerator below instead.
class ExponentialSignalGenerator {
  public:
    ExponentialSignalGenerator(TimeTaggerBase * /*tagger*/, double /*rate*/,
                                channel_t base_channel = CHANNEL_UNUSED,
                                std::int32_t /*seed*/ = -1)
        : channel_(base_channel != CHANNEL_UNUSED
                        ? base_channel
                        : internal::AllocateVirtualChannel()) {}

    [[nodiscard]] channel_t getChannel() const { return channel_; }

  private:
    channel_t channel_;
};

// Fake of Experimental::PatternSignalGenerator. Produces events on
// `base_channel` (or a freshly allocated channel if base_channel is
// CHANNEL_UNUSED, matching real semantics) following a deterministic
// timestamp pattern: `sequence` gives offsets (ps) within one cycle: if
// `repeat` is true, the whole cycle repeats every `spacing` ps forever;
// otherwise the sequence fires once and the channel then goes quiet. The
// first cycle starts `start_delay` ps after this object is constructed.
//
// For a simple periodic signal (e.g. standing in for a scanner's pixel or
// line clock), use a single-entry sequence: `{tagger, {0}, true,
// 0, period_ps}` produces one rising edge every `period_ps`.
//
// Implemented by registering the pattern into TimeTagger.h's
// ChannelPatternRegistry for the lifetime of this object -- both
// IteratorBase::PumpLoop and TimeTagStream consult that registry, so this
// works the same way regardless of which one is pulling tags for the
// channel.
class PatternSignalGenerator {
  public:
    PatternSignalGenerator(TimeTaggerBase * /*tagger*/,
                            std::vector<timestamp_t> sequence,
                            bool repeat = false, timestamp_t start_delay = 0,
                            timestamp_t spacing = 0,
                            channel_t base_channel = CHANNEL_UNUSED)
        : channel_(base_channel != CHANNEL_UNUSED
                        ? base_channel
                        : internal::AllocateVirtualChannel()) {
        ChannelPatternRegistry::Register(
            channel_, ChannelPatternRegistry::Pattern{
                          std::move(sequence), repeat, start_delay, spacing});
    }

    ~PatternSignalGenerator() { ChannelPatternRegistry::Unregister(channel_); }

    PatternSignalGenerator(PatternSignalGenerator const &) = delete;
    PatternSignalGenerator &operator=(PatternSignalGenerator const &) = delete;

    [[nodiscard]] channel_t getChannel() const { return channel_; }

  private:
    channel_t channel_;
};

} // namespace Experimental
