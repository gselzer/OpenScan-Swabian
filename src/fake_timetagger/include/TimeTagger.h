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
// through a TimeTaggerBase*. Also covers Tag and IteratorBase, since code
// in this project builds a custom IteratorBase subclass to receive raw
// tags. Deliberately NOT covered: TimeTaggerHardware / TimeTaggerNetwork /
// TimeTagger-specific methods (FPGA bitfiles, network server hosting,
// licensing), CustomMeasurementBase (the wrapper-language-oriented
// IteratorBase subclass -- this project subclasses IteratorBase directly,
// like the vendor's own C++ examples do), and IteratorBase's virtual-channel
// allocation / getCaptureDuration() / getConfiguration() -- those only make
// sense for a real device, or aren't used by anything in this project yet.
//
// Most methods here are inert stubs (store-and-return-what-was-set, or a
// fixed plausible default) -- there is no real hardware or clock
// underneath them. Synthetic event generation is implemented here (for
// IteratorBase, via a background thread -- see IteratorBase::PumpLoop)
// and separately in measurements/TimeTagStream.h and
// measurements/SignalGenerators.h (a pull-based equivalent, predating
// IteratorBase support here). Both reuse the same
// TimeTaggerBase::RegisterChannel/GetChannelRate mechanism below.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// Matches the real SDK's own (macro, not typedef) definitions, so values
// stay compatible if code is later built against the real headers.
#define timestamp_t long long
#define channel_t int

constexpr channel_t CHANNEL_UNUSED = 0xf8000000;

class IteratorBase; // Full definition below, after TimeTaggerBase; only
                     // used by pointer/reference up to that point.

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

// A single event delivered from the (simulated) Time Tagger backend.
// Layout matches the real SDK's Tag exactly (1+1+2+4+8 = 16 bytes, no
// padding), so anything that depends on that size/layout behaves the same
// against this fake as against the real header.
struct Tag {
    enum class Type : unsigned char {
        TimeTag = 0,
        Error = 1,
        OverflowBegin = 2,
        OverflowEnd = 3,
        MissedEvents = 4,
    };

    Type type = Type::TimeTag;
    char reserved = 0;
    unsigned short missed_events = 0;
    channel_t channel = 0;
    timestamp_t time = 0;

    Tag() = default;
    Tag(timestamp_t ts, channel_t ch, Type type = Type::TimeTag)
        : type(type), channel(ch), time(ts) {}
};

inline bool operator==(Tag const &a, Tag const &b) {
    return a.type == b.type && a.channel == b.channel && a.time == b.time &&
           a.missed_events == b.missed_events;
}

// Fake of IteratorBase. Drives a background thread that synthesizes
// Poisson-arrival events -- reusing TimeTaggerBase::RegisterChannel's
// per-channel rates, same technique as measurements/TimeTagStream.h, just
// restructured as a push instead of a pull -- for whatever channels the
// subclass registers, and delivers them via next_impl(). This is enough to
// exercise a real next_impl()-based acquisition design under simulate=true
// without hardware; it does not model getCaptureDuration(), getConfiguration(),
// virtual-channel allocation, or startFor()'s auto-stop-after-duration
// (nothing in this project uses those yet).
//
// IMPORTANT, and true of the real SDK too: the pump thread calls next_impl()
// (a pure virtual) until stop() has fully returned. Because base-class
// destructors run *after* the derived class's own destructor body, a
// subclass MUST call stop() itself, near the top of its own destructor,
// before tearing down any state next_impl() touches. ~IteratorBase() also
// calls stop() as a backstop, but by the time it runs, derived state may
// already be gone -- relying on it alone risks next_impl() running against
// a partially-destroyed object.
class IteratorBase {
  public:
    IteratorBase(IteratorBase const &) = delete;
    IteratorBase &operator=(IteratorBase const &) = delete;

    virtual ~IteratorBase() { stop(); }

    void clear() {
        auto lock = getLock();
        clear_impl();
    }

    void start() {
        if (running_.exchange(true))
            return;
        {
            auto lock = getLock();
            on_start();
        }
        pump_thread_ = std::thread(&IteratorBase::PumpLoop, this);
    }

    // Fake-only simplification: does not auto-stop after capture_duration.
    void startFor(timestamp_t /*capture_duration*/, bool clear_first = true) {
        if (clear_first)
            clear();
        start();
    }

    void stop() {
        if (!running_)
            return;
        {
            auto lock = getLock();
            pre_stop();
        }
        running_ = false;
        if (pump_thread_.joinable())
            pump_thread_.join();
        auto lock = getLock();
        on_stop();
    }

    void abort() {
        stop();
        clear();
    }

    [[nodiscard]] bool isRunning() const { return running_; }

    // Matches real semantics ("roughly equivalent to a polling loop with
    // sleep()"): safe to call from any thread, never touches pump_thread_
    // directly.
    bool waitUntilFinished(std::int64_t timeout_ms = -1) {
        auto const deadline = std::chrono::steady_clock::now() +
                               std::chrono::milliseconds(timeout_ms);
        while (running_) {
            if (timeout_ms >= 0 && std::chrono::steady_clock::now() >= deadline)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

  protected:
    // base_type_/extra_info_ accepted for signature compatibility with the
    // real SDK; unused here.
    explicit IteratorBase(TimeTaggerBase *tagger,
                           std::string const & /*base_type_*/ = "IteratorBase",
                           std::string const & /*extra_info_*/ = "")
        : tagger_(tagger) {}

    void registerChannel(channel_t channel) {
        auto lock = getLock();
        registered_channels_.push_back(channel);
    }
    void unregisterChannel(channel_t channel) {
        auto lock = getLock();
        registered_channels_.erase(
            std::remove(registered_channels_.begin(),
                        registered_channels_.end(), channel),
            registered_channels_.end());
    }

    void finishInitialization() { start(); }

    virtual bool next_impl(std::vector<Tag> &incoming_tags,
                            timestamp_t begin_time, timestamp_t end_time) = 0;
    virtual void clear_impl() {}
    virtual void on_start() {}
    virtual void on_stop() {}
    virtual void pre_stop() {}

    std::unique_lock<std::mutex> getLock() {
        return std::unique_lock<std::mutex>(mutex_);
    }

  private:
    void PumpLoop() {
        double elapsed_ps = 0.0;
        auto last = std::chrono::steady_clock::now();
        std::mt19937_64 rng{std::random_device{}()};
        std::map<channel_t, double> next_arrival_ps;

        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

            auto const now = std::chrono::steady_clock::now();
            double const elapsed_s =
                std::chrono::duration_cast<std::chrono::duration<double>>(
                    now - last)
                    .count();
            last = now;
            timestamp_t const begin_time = static_cast<timestamp_t>(elapsed_ps);
            elapsed_ps += elapsed_s * 1e12;
            timestamp_t const end_time = static_cast<timestamp_t>(elapsed_ps);

            auto lock = getLock();
            if (!running_)
                break;

            std::vector<Tag> batch;
            for (channel_t const channel : registered_channels_) {
                double rate_hz;
                try {
                    rate_hz = tagger_->GetChannelRate(channel);
                } catch (std::out_of_range const &) {
                    continue; // no synthetic rate configured for this
                              // channel (e.g. RegisterChannel() was never
                              // called for it); produce no events
                }
                if (rate_hz <= 0)
                    continue;

                std::exponential_distribution<double> interval_dist(rate_hz);
                auto it = next_arrival_ps.find(channel);
                if (it == next_arrival_ps.end())
                    it = next_arrival_ps
                             .emplace(channel, interval_dist(rng) * 1e12)
                             .first;
                while (it->second < elapsed_ps) {
                    batch.emplace_back(static_cast<timestamp_t>(it->second),
                                        channel);
                    it->second += interval_dist(rng) * 1e12;
                }
            }
            std::sort(batch.begin(), batch.end(),
                      [](Tag const &a, Tag const &b) { return a.time < b.time; });

            next_impl(batch, begin_time, end_time);
        }
    }

    TimeTaggerBase *tagger_;
    std::mutex mutex_;
    std::atomic<bool> running_{false};
    std::thread pump_thread_;
    std::vector<channel_t> registered_channels_;
};

// Fake-only: the one serial this fake pretends to have plugged in, and the
// model name it reports for that serial.
inline constexpr char kFakeSerial[] = "SIM000001";
inline constexpr char kFakeModel[] = "Simulated Time Tagger";

// Fake of createTimeTagger(): connects to the fake device if the serial
// matches (or is left empty, meaning "first available"), matching real SDK
// semantics -- empty serial connects to the first device found, a wrong
// serial throws. Does not model the `resolution` parameter or
// createTimeTaggerVirtual/TimeTaggerHardware/TimeTaggerNetwork; nothing in
// this project uses those yet (see file-level comment above).
inline TimeTaggerBase *createTimeTagger(std::string const &serial = "") {
    if (!serial.empty() && serial != kFakeSerial) {
        throw std::runtime_error("No Time Tagger device with serial '" +
                                  serial + "' found");
    }
    return new TimeTaggerBase();
}

inline void freeTimeTagger(TimeTaggerBase *tagger) { delete tagger; }

// Fake-only: no real hardware to scan, so return the one fixed placeholder
// serial (matching real SDK's "serial,model" format when requested), so
// callers can exercise the same enumerate-then-connect path used with real
// hardware without branching on build mode.
inline std::vector<std::string> scanTimeTagger(bool include_model_name = false) {
    if (include_model_name)
        return {std::string(kFakeSerial) + "," + kFakeModel};
    return {kFakeSerial};
}

// Fake of getTimeTaggerModel(): reports the model for the one serial this
// fake knows about. The real SDK's doc comment doesn't specify behavior for
// an unrecognized serial; we throw, matching createTimeTagger()'s handling
// of a wrong serial above.
inline std::string getTimeTaggerModel(std::string const &serial) {
    if (serial != kFakeSerial) {
        throw std::runtime_error("No Time Tagger device with serial '" +
                                  serial + "' found");
    }
    return kFakeModel;
}
