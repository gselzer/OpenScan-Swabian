#pragma once

// A fake of Swabian Instruments' Time Tagger C++ SDK
// (https://www.swabianinstruments.com/static/documentation/TimeTagger/).
// Selected in place of the real vendor SDK headers by meson.build's
// `simulate` option (via include path ordering), so code written against
// the real API can build and run without physical hardware or the SDK's
// hardware-backed license check.
//
// Scope: the public API surface of TimeTaggerBase -- the interface that
// measurement/virtual-channel constructors actually take as a parameter
// (real code holds a `TimeTaggerBase *`, never the concrete TimeTagger /
// TimeTaggerVirtual / TimeTaggerNetwork types directly). The real SDK
// splits this across two classes (TimeTaggerSource, which TimeTaggerBase
// publicly inherits from, plus TimeTaggerBase itself); we flatten both
// into one class here since the split doesn't affect what's callable
// through a TimeTaggerBase*. Deliberately NOT covered: TimeTaggerHardware
// / TimeTaggerNetwork / TimeTagger-specific methods (FPGA bitfiles,
// network server hosting, licensing) -- those only make sense for a real
// or networked device, and the protected methods TimeTaggerBase uses
// internally to talk to its own IteratorBase machinery, since our fake
// measurement classes (see measurements/) don't go through that
// mechanism at all.
//
// Most methods here are inert stubs (store-and-return-what-was-set, or a
// fixed plausible default) -- there is no real hardware, clock, or event
// pipeline underneath them. Actual synthetic event generation is
// implemented separately (see measurements/TimeTagStream.h and
// measurements/SignalGenerators.h).

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

// Matches the real SDK's own (macro, not typedef) definitions, so values
// stay compatible if code is later built against the real headers.
#define timestamp_t long long
#define channel_t int

constexpr channel_t CHANNEL_UNUSED = 0xf8000000;

class IteratorBase; // Not implemented by this fake; only used by pointer.

struct SoftwareClockState {
    timestamp_t clock_period = 0;
    channel_t input_channel = CHANNEL_UNUSED;
    channel_t ideal_clock_channel = CHANNEL_UNUSED;
    double averaging_periods = 0;
    bool enabled = false;
    bool is_locked = false;
    std::uint32_t error_counter = 0;
    timestamp_t last_ideal_clock_event = 0;
    double period_error = 0;
    double phase_error_estimation = 0;
};

struct ReferenceClockState {
    timestamp_t clock_period = 0;
    channel_t clock_channel = CHANNEL_UNUSED;
    channel_t synchronization_channel = CHANNEL_UNUSED;
    channel_t ideal_clock_channel = CHANNEL_UNUSED;
    double averaging_periods = 0;
    timestamp_t synchronization_offset = 0;
    bool enabled = false;
    int event_divider = 1;
    bool is_locked = false;
    bool is_synchronized = false;
    std::uint32_t error_counter = 0;
    timestamp_t last_ideal_clock_event = 0;
    double period_error = 0;
    double phase_error_estimation = 0;
};

class TimeTaggerBase {
  public:
    TimeTaggerBase() = default;
    virtual ~TimeTaggerBase() = default;

    TimeTaggerBase(TimeTaggerBase const &) = delete;
    TimeTaggerBase &operator=(TimeTaggerBase const &) = delete;

    // --- fake-only extensions: NOT part of the real SDK -----------------
    // Used by this project's own fake measurement classes (see
    // measurements/SignalGenerators.h, measurements/TimeTagStream.h) to
    // register and look up a simulated channel's event rate.

    // Registers a new channel with the given aggregate arrival rate (Hz)
    // and returns its channel number.
    channel_t RegisterChannel(double rate_hz) {
        channel_t const channel = next_channel_++;
        channel_rates_hz_[channel] = rate_hz;
        return channel;
    }

    // Precondition: channel was previously returned by RegisterChannel().
    [[nodiscard]] double GetChannelRate(channel_t channel) const {
        return channel_rates_hz_.at(channel);
    }

    // --- from the real SDK's TimeTaggerSource ---------------------------

    void setInputDelay(channel_t channel, timestamp_t delay) {
        input_delay_ps_[channel] = delay;
    }
    [[nodiscard]] timestamp_t getInputDelay(channel_t channel) {
        return input_delay_ps_[channel];
    }

    void setDelayHardware(channel_t channel, timestamp_t delay) {
        hardware_delay_ps_[channel] = delay;
    }
    [[nodiscard]] timestamp_t getDelayHardware(channel_t channel) {
        return hardware_delay_ps_[channel];
    }
    [[nodiscard]] std::vector<timestamp_t> getDelayHardwareRange(channel_t) {
        return {-2'500'000, 2'500'000}; // plausible ttx-like range, in ps
    }

    void setDelaySoftware(channel_t channel, timestamp_t delay) {
        software_delay_ps_[channel] = delay;
    }
    [[nodiscard]] timestamp_t getDelaySoftware(channel_t channel) {
        return software_delay_ps_[channel];
    }

    timestamp_t setDeadtime(channel_t channel, timestamp_t deadtime) {
        return deadtime_ps_[channel] = deadtime;
    }
    [[nodiscard]] timestamp_t getDeadtime(channel_t channel) {
        return deadtime_ps_[channel];
    }
    [[nodiscard]] std::vector<timestamp_t> getDeadtimeRange(channel_t) {
        return {0, 716'000'000}; // plausible ttx-like max, in ps
    }

    void setConditionalFilter(std::vector<channel_t> trigger,
                               std::vector<channel_t> filtered) {
        conditional_filter_trigger_ = std::move(trigger);
        conditional_filter_filtered_ = std::move(filtered);
    }
    void clearConditionalFilter() { setConditionalFilter({}, {}); }
    [[nodiscard]] std::vector<channel_t> getConditionalFilterTrigger() {
        return conditional_filter_trigger_;
    }
    [[nodiscard]] std::vector<channel_t> getConditionalFilterFiltered() {
        return conditional_filter_filtered_;
    }

    void setEventDivider(channel_t channel, unsigned int divider) {
        event_divider_[channel] = divider;
    }
    [[nodiscard]] unsigned int getEventDivider(channel_t channel) {
        auto const it = event_divider_.find(channel);
        return it == event_divider_.end() ? 1 : it->second;
    }

    [[nodiscard]] long long getOverflows() const { return overflow_count_; }
    long long getOverflowsAndClear() {
        long long const count = overflow_count_;
        overflow_count_ = 0;
        return count;
    }
    void clearOverflows() { overflow_count_ = 0; }

    void setReferenceClock(channel_t clock_channel, double clock_frequency = 10e6,
                            double = 1e-3,
                            channel_t synchronization_channel = CHANNEL_UNUSED,
                            timestamp_t synchronization_offset = 0, bool = true) {
        reference_clock_state_.clock_channel = clock_channel;
        reference_clock_state_.clock_period =
            static_cast<timestamp_t>(1e12 / clock_frequency);
        reference_clock_state_.synchronization_channel = synchronization_channel;
        reference_clock_state_.synchronization_offset = synchronization_offset;
        reference_clock_state_.enabled = true;
        reference_clock_state_.is_locked = true; // nothing to fail to lock to
    }
    void disableReferenceClock() { reference_clock_state_.enabled = false; }
    [[nodiscard]] ReferenceClockState getReferenceClockState() const {
        return reference_clock_state_;
    }

    // --- from the real SDK's TimeTaggerBase -----------------------------

    void setSoftwareClock(channel_t input_channel, double input_frequency = 10e6,
                           double averaging_periods = 1000, bool = true) {
        software_clock_state_.input_channel = input_channel;
        software_clock_state_.clock_period =
            static_cast<timestamp_t>(1e12 / input_frequency);
        software_clock_state_.averaging_periods = averaging_periods;
        software_clock_state_.enabled = true;
        software_clock_state_.is_locked = true;
    }
    void disableSoftwareClock() { software_clock_state_.enabled = false; }
    [[nodiscard]] SoftwareClockState getSoftwareClockState() const {
        return software_clock_state_;
    }

    [[nodiscard]] unsigned int getFence(bool alloc_fence = true) {
        return alloc_fence ? ++next_fence_ : next_fence_;
    }
    [[nodiscard]] bool waitForFence(unsigned int, std::int64_t = -1) {
        return true; // no pipeline to wait on
    }
    [[nodiscard]] bool sync(std::int64_t = -1) { return true; }

    [[nodiscard]] channel_t getInvertedChannel(channel_t) {
        return CHANNEL_UNUSED; // no inverted channels modeled
    }
    [[nodiscard]] bool isUnusedChannel(channel_t channel) {
        return channel == CHANNEL_UNUSED;
    }
    [[nodiscard]] std::string getConfiguration() { return "{}"; }

    using IteratorCallback = std::function<void(IteratorBase *)>;
    using IteratorCallbackMap = std::map<IteratorBase *, IteratorCallback>;
    void runSynchronized(IteratorCallbackMap const &callbacks, bool = true) {
        for (auto const &[iterator, callback] : callbacks)
            callback(iterator);
    }

    [[nodiscard]] int getRegistrations(channel_t) {
        return 0; // unrelated to RegisterChannel() above; not tracked
    }

    void xtra_setAutoStart(bool auto_start) { auto_start_ = auto_start; }
    [[nodiscard]] bool xtra_getAutoStart() const { return auto_start_; }

  private:
    std::map<channel_t, double> channel_rates_hz_;
    channel_t next_channel_ = 1;

    std::map<channel_t, timestamp_t> input_delay_ps_;
    std::map<channel_t, timestamp_t> hardware_delay_ps_;
    std::map<channel_t, timestamp_t> software_delay_ps_;
    std::map<channel_t, timestamp_t> deadtime_ps_;
    std::map<channel_t, unsigned int> event_divider_;

    std::vector<channel_t> conditional_filter_trigger_;
    std::vector<channel_t> conditional_filter_filtered_;

    long long overflow_count_ = 0;
    unsigned int next_fence_ = 0;
    bool auto_start_ = true;

    ReferenceClockState reference_clock_state_;
    SoftwareClockState software_clock_state_;
};

inline TimeTaggerBase *createTimeTaggerVirtual(std::string const & = "",
                                                timestamp_t = 0,
                                                timestamp_t = -1) {
    return new TimeTaggerBase();
}

inline void freeTimeTagger(TimeTaggerBase *tagger) { delete tagger; }
