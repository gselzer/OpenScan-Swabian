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
// IteratorBase support here). Both generate events for ANY channel at the
// same fixed rate, kDefaultChannelRateHz below -- there is deliberately no
// API anywhere on this fake (TimeTaggerBase or otherwise) for configuring
// a channel's simulated rate, since no such API exists on the real
// TimeTaggerBase either. In particular, this means a device module's own
// settings (e.g. which channel is the sync/pixel-marker/photon input) are
// free to pick any channel number -- in simulate mode, whichever channels
// end up configured will simply start producing generic synthetic events
// at kDefaultChannelRateHz; there is no per-role or per-channel modeling.
//
// One exception, and it's not really an exception: a channel can be made
// to follow a deterministic pattern instead, but only by constructing an
// explicit signal-generator object for it (see ChannelPatternRegistry
// below and measurements/SignalGenerators.h's PatternSignalGenerator) --
// real, documented API surface on the real SDK too. There is still no
// TimeTaggerBase (or any other) method that takes a channel number and a
// rate/pattern directly.
//
// A second, genuine exception: kSimulatedLineClockChannel below. Unlike
// PatternSignalGenerator, this one really does break the "no per-channel
// modeling" rule -- it is a hardcoded channel number, baked into this fake
// with no way for device-module code to configure or discover it via any
// API call. It exists because a scanner's line clock, on real hardware,
// requires no Time Tagger API call at all to appear on an input channel --
// it's just physically wired in. A device module written against this API
// therefore has no call available to make an equivalent signal appear in
// simulate mode either (constructing a PatternSignalGenerator targeting an
// already-externally-wired input channel would be meaningless, or actively
// wrong, on real hardware, so it can't be something module code calls
// unconditionally). This fake instead pretends its one simulated device
// always has a scanner's line clock wired into this one fixed channel,
// exactly as if that were part of the simulated hardware's fixed identity
// (compare kFakeSerial/kFakeModel below).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
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

// Fake-only: the synthetic arrival rate (Hz) used for every channel, in
// both IteratorBase::PumpLoop and measurements/TimeTagStream.h. Not
// configurable anywhere, on purpose -- see the file comment above.
constexpr double kDefaultChannelRateHz = 100'000.0;

// Fake-only, currently UNUSED (not wired into PumpLoop or
// TimeTagStream::getData() -- see EventPipeline.cpp's TODO next to its
// stop_with_error for lost-interval events). The idea: if a single
// PumpLoop iteration (or getData() call) ever needed to advance by more
// simulated time than this, that's the fake's analog of a real device's
// circular FIFO overflowing because software fell behind -- the plan is
// to emit synthetic OverflowBegin/OverflowEnd/MissedEvents tags for the
// excess span (matching real hardware) rather than either silently
// retaining every tag no matter how large the backlog gets, or silently
// dropping/delaying data with no signal to software at all.
constexpr timestamp_t kMaxSimulatedStepPs = 10'000'000; // 10 ms

// Fake-only: see the file comment's second exception, above. This one
// fixed channel always carries a periodic signal (unless a
// PatternSignalGenerator has been explicitly constructed for it, which
// still takes priority -- see PumpLoop and TimeTagStream::TimeTagStream()
// below), standing in for a scanner's line clock. The period (~250 kHz)
// matches a real Time Tagger line-clock capture this project's device
// module was validated against; it is a fixed fake-hardware constant, not
// derived from any acquisition's pixel rate or ROI width -- this fake has
// no access to those OpenScan-specific concepts, and no API call is needed
// from module code to make this channel periodic in the first place.
constexpr channel_t kSimulatedLineClockChannel = 1;
// Sized to match the pipeline's currently-configured default ROI (512x512)
// at kSimulatedPixelPeriodPs (see below): 512 pixels/line *
// kSimulatedPixelPeriodPs = 0.512 ms/line, so pixel_marker_processor's
// width+1 generated pixel ticks exactly fill one simulated line period,
// with no dead time where sync/photon keep firing but there's no active
// pixel window to bin them into (this fake can't read the acquisition's
// actual pixel rate or ROI width -- see the file comment above -- so this
// is necessarily a fixed assumption, not derived; if the configured ROI
// width OR kSimulatedPixelPeriodPs changes, this should be recomputed to
// match: width * kSimulatedPixelPeriodPs). This fake's generation rate
// isn't accelerated -- it runs at whatever rate these constants specify,
// in real time, so 512 lines (one frame) takes ~0.26 s of both simulated
// AND wall-clock time (assuming the pipeline can actually keep up -- see
// kSimulatedPixelPeriodPs's own comment on that).
constexpr timestamp_t kSimulatedLinePeriodPs = 512'000'000; // ~1.95 kHz (512 px/line * kSimulatedPixelPeriodPs)

// Fake-only: same exception as kSimulatedLineClockChannel above, extended
// to a simulated laser-sync + photon pair so the sync/photon time
// correlation this project's pipeline depends on (pair_all_between +
// time_correlate_at_stop in EventPipeline.cpp) has something to actually
// correlate in simulate mode. Two channel numbers, matching the default
// syncChannel/photonChannel settings:
//
//  - kSimulatedSyncChannel (and its negative) get a periodic +/- pulse
//    pair, one per kSimulatedPixelPeriodPs, via the same
//    ChannelPatternRegistry::Pattern mechanism as the line clock.
//  - kSimulatedPhotonChannel (and its negative) get Poisson-arrival +/-
//    pulse pairs, but *gated*: arrivals are only generated within
//    kSimulatedPhotonGateWidthPs after each sync period's start, then
//    silent until the next period. This can't be expressed as a
//    ChannelPatternRegistry::Pattern (that's a fixed deterministic
//    sequence, not a bounded random process), so PumpLoop and
//    TimeTagStream::getData() special-case it directly, generating both
//    polarities together from one shared draw sequence (not
//    independently per channel) so the rising/falling tags stay paired
//    for pair_one_between's pulse-width matching downstream.
constexpr channel_t kSimulatedSyncChannel = 2;
constexpr channel_t kSimulatedPhotonChannel = 3;
// Temporarily scaled down from a realistic ~10 MHz sync rate -- see
// kSimulatedLinePeriodPs's comment above. At 10 MHz, sync alone (2 tags/
// period) is ~20M tags/sec, and gated photon noise (~1 detection/period,
// 2 tags) adds roughly another ~20M tags/sec -- both far beyond what this
// pipeline can sustain. At the 1 MHz used here, aggregate throughput is
// instead ~2M (sync) + ~2M (photon) = ~4M tags/sec.
//
// UNVERIFIED at this rate: the last actual throughput measurement (~800K
// tags/sec, from a real next_impl() call) predates the tag-buffer/
// consumer-thread split, the batch/unbatch optimization around it, AND
// the full-histogram/live-intensity broadcast split (which roughly
// doubles per-tag consumer work, since every tag now flows through two
// histogram branches instead of one) -- so it's unknown whether 4M
// tags/sec is actually sustainable now. If PumpLoop's batches start
// growing without bound (the same runaway pattern chased down earlier in
// this project's history), that's this rate exceeding whatever the
// current real ceiling is; kMaxSimulatedStepPs (unused -- see its own
// comment) and/or the synthetic-overflow-tags TODO in EventPipeline.cpp
// are the real fixes, not further lowering this constant.
constexpr timestamp_t kSimulatedPixelPeriodPs = 1'000'000;   // 1 us (1 MHz) between sync pulses
constexpr timestamp_t kSimulatedSyncPulseWidth = 1'000;       // ps, sync rising -> falling
constexpr timestamp_t kSimulatedPhotonPulseWidth = 2'000;     // ps, photon rising -> falling
constexpr timestamp_t kSimulatedPhotonGateWidthPs = 10'000;   // ps after each sync tick where photon noise can occur
constexpr double kSimulatedPhotonGateRateHz = 1e8;            // Poisson rate while the gate is open (~1 photon/gate; independent of the period above -- see mean = rate * gate_width)

// Fake-only: lets a signal generator (e.g. PatternSignalGenerator, in
// measurements/SignalGenerators.h) override the default Poisson synthesis
// for one channel with a deterministic timestamp pattern, for as long as
// the generator object is alive. Consulted independently by every
// IteratorBase::PumpLoop and TimeTagStream instance -- each such consumer
// walks its own copy of the pattern from its own local start, so this
// does NOT give multiple simultaneous listeners on the same channel an
// identical tag sequence (same caveat as the rest of this file's
// synthetic generation; see the multi-iterator note near IteratorBase).
// The only way to populate this is by constructing a signal-generator
// object -- there is still no direct "set this channel's pattern" method
// on TimeTaggerBase or anything else.
class ChannelPatternRegistry {
  public:
    struct Pattern {
        std::vector<timestamp_t> sequence; // offsets within one cycle, ps
        bool repeat = false;
        timestamp_t start_delay = 0; // ps before the first cycle
        timestamp_t spacing = 0;     // ps between the start of each cycle
    };

    static void Register(channel_t channel, Pattern pattern) {
        std::lock_guard<std::mutex> lock(GetMutex());
        GetMap()[channel] = std::move(pattern);
    }
    static void Unregister(channel_t channel) {
        std::lock_guard<std::mutex> lock(GetMutex());
        GetMap().erase(channel);
    }
    [[nodiscard]] static std::optional<Pattern> Find(channel_t channel) {
        std::lock_guard<std::mutex> lock(GetMutex());
        auto const it = GetMap().find(channel);
        if (it == GetMap().end())
            return std::nullopt;
        return it->second;
    }

  private:
    static std::mutex &GetMutex() {
        static std::mutex m;
        return m;
    }
    static std::map<channel_t, Pattern> &GetMap() {
        static std::map<channel_t, Pattern> m;
        return m;
    }
};

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
        return 0; // not tracked
    }

    void xtra_setAutoStart(bool auto_start) { auto_start_ = auto_start; }
    [[nodiscard]] bool xtra_getAutoStart() const { return auto_start_; }

  private:
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
// Poisson-arrival events at the fixed kDefaultChannelRateHz -- same
// technique as measurements/TimeTagStream.h, just restructured as a push
// instead of a pull -- for whatever channels the subclass registers, and
// delivers them via next_impl(). This is enough to
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
        // Guard on joinable(), not running_: finish_running() (below) can
        // set running_ = false without joining pump_thread_, so a
        // running_-only guard would leave that thread unjoined forever --
        // std::terminate() the next time something tries to reuse or
        // destroy pump_thread_. joinable() correctly stays true in that
        // case, and becomes false only once actually joined, making this
        // safe to call again afterward (idempotent).
        if (!pump_thread_.joinable())
            return;
        {
            auto lock = getLock();
            pre_stop();
        }
        running_ = false;
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
    // real SDK; unused here. tagger_ mirrors the real SDK's own protected
    // `tagger` member; kept for the same reason even though nothing in
    // this fake's own PumpLoop needs to read it back.
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

    // Lets a measurement stop itself from inside next_impl() (directly, or
    // via a callback invoked from there) without deadlocking: unlike
    // stop(), this does not acquire the lock or join pump_thread_, both of
    // which would be unsafe here since next_impl() is called by
    // PumpLoop() while already holding that same lock, on that same
    // thread. Matches the real SDK's finish_running(), including its
    // precondition ("shall only be called while the measurement mutex is
    // locked") and the fact that it does NOT call on_stop() for you --
    // call that yourself afterward if cleanup needs to run.
    //
    // Ensures no further data is delivered (PumpLoop's while(running_)
    // check will see this on its next iteration and exit), but leaves
    // pump_thread_ unjoined -- a later stop() (e.g. from ~EventPipeline()
    // or an explicit Stop()) still needs to run to actually join it and
    // call on_stop(). Until then, isRunning() correctly reports false
    // even though the object is still alive, matching how BH's acqState
    // stays alive after an acquisition finishes on its own.
    void finish_running() { running_ = false; }

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
        std::map<channel_t, std::size_t> next_pattern_index;
        std::optional<double> next_photon_candidate_ps;

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

            bool const photon_pos_registered =
                std::find(registered_channels_.begin(),
                          registered_channels_.end(),
                          kSimulatedPhotonChannel) != registered_channels_.end();
            bool const photon_neg_registered =
                std::find(registered_channels_.begin(),
                          registered_channels_.end(),
                          -kSimulatedPhotonChannel) != registered_channels_.end();
            if (photon_pos_registered || photon_neg_registered) {
                // Gated Poisson photon noise correlated to the simulated
                // sync channel (see kSimulatedPhotonChannel's comment
                // above). Both polarities are generated together here, from
                // one shared draw sequence, so pair_one_between finds a
                // matching rising/falling pair for every simulated photon
                // instead of two independently drifting tag streams.
                std::exponential_distribution<double> gate_dist(
                    kSimulatedPhotonGateRateHz);
                if (!next_photon_candidate_ps)
                    next_photon_candidate_ps = gate_dist(rng) * 1e12;
                for (;;) {
                    double &t = *next_photon_candidate_ps;
                    if (t >= elapsed_ps)
                        break;
                    auto const period_index = static_cast<std::int64_t>(
                        t / static_cast<double>(kSimulatedPixelPeriodPs));
                    double const period_start =
                        static_cast<double>(period_index) *
                        static_cast<double>(kSimulatedPixelPeriodPs);
                    double const offset_in_period = t - period_start;
                    if (offset_in_period <
                        static_cast<double>(kSimulatedPhotonGateWidthPs)) {
                        if (photon_pos_registered &&
                            !ChannelPatternRegistry::Find(
                                kSimulatedPhotonChannel))
                            batch.emplace_back(static_cast<timestamp_t>(t),
                                                kSimulatedPhotonChannel);
                        if (photon_neg_registered &&
                            !ChannelPatternRegistry::Find(
                                -kSimulatedPhotonChannel))
                            batch.emplace_back(
                                static_cast<timestamp_t>(
                                    t + kSimulatedPhotonPulseWidth),
                                -kSimulatedPhotonChannel);
                        // Schedule the next candidate from this pulse's
                        // FALLING edge, not its rising edge -- otherwise a
                        // short draw can land the next rising edge before
                        // this pulse's falling edge, producing two rising
                        // transitions in a row on the same channel, which
                        // is not physically possible for a real detector
                        // pulse (a single digital line's edges must
                        // strictly alternate).
                        t += static_cast<double>(kSimulatedPhotonPulseWidth) +
                             gate_dist(rng) * 1e12;
                    } else {
                        // Past this period's gate -- jump straight to the
                        // start of the next one instead of continuing to
                        // draw candidates that would only land in dead
                        // time.
                        t = period_start +
                            static_cast<double>(kSimulatedPixelPeriodPs);
                    }
                }
            }

            for (channel_t const channel : registered_channels_) {
                if (channel == kSimulatedPhotonChannel ||
                    channel == -kSimulatedPhotonChannel) {
                    // Handled above, as a correlated rising/falling pair,
                    // unless something explicitly registered a pattern for
                    // this exact polarity.
                    if (!ChannelPatternRegistry::Find(channel))
                        continue;
                }

                std::optional<ChannelPatternRegistry::Pattern> pattern =
                    ChannelPatternRegistry::Find(channel);
                if (!pattern && channel == kSimulatedLineClockChannel) {
                    // Hardcoded simulated line clock (see the constant's
                    // own comment above) -- only when nothing has
                    // explicitly registered a pattern for this channel, so
                    // an explicit PatternSignalGenerator still wins.
                    pattern = ChannelPatternRegistry::Pattern{
                        {0}, true, 0, kSimulatedLinePeriodPs};
                }
                if (!pattern && channel == kSimulatedSyncChannel) {
                    pattern = ChannelPatternRegistry::Pattern{
                        {0}, true, 0, kSimulatedPixelPeriodPs};
                }
                if (!pattern && channel == -kSimulatedSyncChannel) {
                    pattern = ChannelPatternRegistry::Pattern{
                        {kSimulatedSyncPulseWidth}, true, 0,
                        kSimulatedPixelPeriodPs};
                }
                if (pattern) {
                    // Deterministic pattern (from PatternSignalGenerator, or
                    // the hardcoded simulated line clock above) instead of
                    // generic Poisson noise. Absolute time of the n-th
                    // sequence entry is start_delay + cycle*spacing +
                    // sequence[offset_index].
                    if (pattern->sequence.empty())
                        continue;
                    std::size_t const cycle_len = pattern->sequence.size();
                    std::size_t &next_index = next_pattern_index[channel];
                    for (;;) {
                        std::size_t const cycle = next_index / cycle_len;
                        if (!pattern->repeat && cycle > 0)
                            break; // one-shot pattern already fully emitted
                        std::size_t const offset_index = next_index % cycle_len;
                        double const t =
                            static_cast<double>(pattern->start_delay) +
                            static_cast<double>(cycle) *
                                static_cast<double>(pattern->spacing) +
                            static_cast<double>(pattern->sequence[offset_index]);
                        if (t >= elapsed_ps)
                            break;
                        batch.emplace_back(static_cast<timestamp_t>(t), channel);
                        ++next_index;
                    }
                    continue;
                }

                std::exponential_distribution<double> interval_dist(
                    kDefaultChannelRateHz);
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
