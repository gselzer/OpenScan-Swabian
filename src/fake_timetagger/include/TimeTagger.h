#pragma once

// A mock of Swabian Instruments' Time Tagger C++ SDK
// (https://www.swabianinstruments.com/static/documentation/TimeTagger/).
// Selected in place of the real vendor SDK headers by meson.build's
// `simulate` option (via include path ordering), so code written against
// the real API can build and run without physical hardware or the SDK's
// hardware-backed license check.
//
// This file is heavily AI-generated, and most methods here are stubs
// (store-and-return-what-was-set, or a fixed plausible default) --
// there is no real hardware or clock underneath them. Synthetic event
// generation is implemented here, for IteratorBase, via a background
// thread -- see IteratorBase::PumpLoop. There is deliberately no API
// anywhere on this mock (TimeTaggerBase or otherwise) for configuring
// a channel's simulated rate or pattern, since no such API exists on the
// real TimeTaggerBase either.

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

// Variables pertaining to data generation parameters. The device module must
// be synchronized with these values to ensure proper operation.
constexpr channel_t LINE_CLOCK_CHANNEL = 1;
constexpr channel_t SYNC_CHANNEL = 2;
constexpr channel_t PHOTON_CHANNEL = 3;

constexpr timestamp_t MAX_SIMULATED_STEP_PS = 10'000'000;

constexpr timestamp_t SIMULATED_LINE_WIDTH_PIXELS = 512;
constexpr timestamp_t SIMULATED_PIXEL_PERIOD_PS = 1'000'000;
constexpr timestamp_t SIMULATED_SYNC_PULSE_WIDTH_PS = 1'000;
constexpr timestamp_t SIMULATED_PHOTON_PULSE_WIDTH_PS = 2'000;
constexpr timestamp_t SIMULATED_PHOTON_GATE_WIDTH_PS = 10'000;
constexpr double SIMULATED_PHOTON_GATE_RATE_HZ = 1e8;

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
        inputDelayPs_[channel] = delay;
    }
    [[nodiscard]] timestamp_t getInputDelay(channel_t channel) {
        return inputDelayPs_[channel];
    }

    void setDelayHardware(channel_t channel, timestamp_t delay) {
        hardwareDelayPs_[channel] = delay;
    }
    [[nodiscard]] timestamp_t getDelayHardware(channel_t channel) {
        return hardwareDelayPs_[channel];
    }
    [[nodiscard]] std::vector<timestamp_t> getDelayHardwareRange(channel_t) {
        return {-2'500'000, 2'500'000}; // plausible ttx-like range, in ps
    }

    void setDelaySoftware(channel_t channel, timestamp_t delay) {
        softwareDelayPs_[channel] = delay;
    }
    [[nodiscard]] timestamp_t getDelaySoftware(channel_t channel) {
        return softwareDelayPs_[channel];
    }

    timestamp_t setDeadtime(channel_t channel, timestamp_t deadtime) {
        return deadtimePs_[channel] = deadtime;
    }
    [[nodiscard]] timestamp_t getDeadtime(channel_t channel) {
        return deadtimePs_[channel];
    }
    [[nodiscard]] std::vector<timestamp_t> getDeadtimeRange(channel_t) {
        return {0, 716'000'000}; // plausible ttx-like max, in ps
    }

    void setConditionalFilter(std::vector<channel_t> trigger,
                               std::vector<channel_t> filtered) {
        conditionalFilterTrigger_ = std::move(trigger);
        conditionalFilterFiltered_ = std::move(filtered);
    }
    void clearConditionalFilter() { setConditionalFilter({}, {}); }
    [[nodiscard]] std::vector<channel_t> getConditionalFilterTrigger() {
        return conditionalFilterTrigger_;
    }
    [[nodiscard]] std::vector<channel_t> getConditionalFilterFiltered() {
        return conditionalFilterFiltered_;
    }

    void setEventDivider(channel_t channel, unsigned int divider) {
        eventDivider_[channel] = divider;
    }
    [[nodiscard]] unsigned int getEventDivider(channel_t channel) {
        auto const it = eventDivider_.find(channel);
        return it == eventDivider_.end() ? 1 : it->second;
    }

    [[nodiscard]] long long getOverflows() const { return overflowCount_; }
    long long getOverflowsAndClear() {
        long long const count = overflowCount_;
        overflowCount_ = 0;
        return count;
    }
    void clearOverflows() { overflowCount_ = 0; }

    void setReferenceClock(channel_t clock_channel, double clock_frequency = 10e6,
                            double = 1e-3,
                            channel_t synchronization_channel = CHANNEL_UNUSED,
                            timestamp_t synchronization_offset = 0, bool = true) {
        referenceClockState_.clock_channel = clock_channel;
        referenceClockState_.clock_period =
            static_cast<timestamp_t>(1e12 / clock_frequency);
        referenceClockState_.synchronization_channel = synchronization_channel;
        referenceClockState_.synchronization_offset = synchronization_offset;
        referenceClockState_.enabled = true;
        referenceClockState_.is_locked = true; // nothing to fail to lock to
    }
    void disableReferenceClock() { referenceClockState_.enabled = false; }
    [[nodiscard]] ReferenceClockState getReferenceClockState() const {
        return referenceClockState_;
    }

    // --- from the real SDK's TimeTaggerBase -----------------------------

    void setSoftwareClock(channel_t input_channel, double input_frequency = 10e6,
                           double averaging_periods = 1000, bool = true) {
        softwareClockState_.input_channel = input_channel;
        softwareClockState_.clock_period =
            static_cast<timestamp_t>(1e12 / input_frequency);
        softwareClockState_.averaging_periods = averaging_periods;
        softwareClockState_.enabled = true;
        softwareClockState_.is_locked = true;
    }
    void disableSoftwareClock() { softwareClockState_.enabled = false; }
    [[nodiscard]] SoftwareClockState getSoftwareClockState() const {
        return softwareClockState_;
    }

    [[nodiscard]] unsigned int getFence(bool allocFence = true) {
        return allocFence ? ++nextFence_ : nextFence_;
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

    void xtra_setAutoStart(bool autoStart) { autoStart_ = autoStart; }
    [[nodiscard]] bool xtra_getAutoStart() const { return autoStart_; }

  private:
    std::map<channel_t, timestamp_t> inputDelayPs_;
    std::map<channel_t, timestamp_t> hardwareDelayPs_;
    std::map<channel_t, timestamp_t> softwareDelayPs_;
    std::map<channel_t, timestamp_t> deadtimePs_;
    std::map<channel_t, unsigned int> eventDivider_;

    std::vector<channel_t> conditionalFilterTrigger_;
    std::vector<channel_t> conditionalFilterFiltered_;

    long long overflowCount_ = 0;
    unsigned int nextFence_ = 0;
    bool autoStart_ = true;

    ReferenceClockState referenceClockState_;
    SoftwareClockState softwareClockState_;
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

// Fake of IteratorBase. Drives a background thread that synthesizes events
// -- the hardcoded line-clock/sync/gated-photon behavior for the three
// special channels (see the file comment above), and nothing at all for
// any other registered channel, since this fake only models the channels
// this project actually wires up -- and delivers them via next_impl(). This
// is enough to
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
        pumpThread_ = std::thread(&IteratorBase::PumpLoop, this);
    }

    // Fake-only simplification: does not auto-stop after capture_duration.
    void startFor(timestamp_t /*capture_duration*/, bool clearFirst = true) {
        if (clearFirst)
            clear();
        start();
    }

    void stop() {
        // Guard on joinable(), not running_: finish_running() (below) can
        // set running_ = false without joining pumpThread_, so a
        // running_-only guard would leave that thread unjoined forever --
        // std::terminate() the next time something tries to reuse or
        // destroy pumpThread_. joinable() correctly stays true in that
        // case, and becomes false only once actually joined, making this
        // safe to call again afterward (idempotent).
        if (!pumpThread_.joinable())
            return;
        {
            auto lock = getLock();
            pre_stop();
        }
        running_ = false;
        pumpThread_.join();
        auto lock = getLock();
        on_stop();
    }

    void abort() {
        stop();
        clear();
    }

    [[nodiscard]] bool isRunning() const { return running_; }

    // Matches real semantics ("roughly equivalent to a polling loop with
    // sleep()"): safe to call from any thread, never touches pumpThread_
    // directly.
    bool waitUntilFinished(std::int64_t timeoutMs = -1) {
        auto const deadline = std::chrono::steady_clock::now() +
                               std::chrono::milliseconds(timeoutMs);
        while (running_) {
            if (timeoutMs >= 0 && std::chrono::steady_clock::now() >= deadline)
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
        registeredChannels_.push_back(channel);
    }
    void unregisterChannel(channel_t channel) {
        auto lock = getLock();
        registeredChannels_.erase(
            std::remove(registeredChannels_.begin(),
                        registeredChannels_.end(), channel),
            registeredChannels_.end());
    }

    void finishInitialization() { start(); }

    // Lets a measurement stop itself from inside next_impl() (directly, or
    // via a callback invoked from there) without deadlocking: unlike
    // stop(), this does not acquire the lock or join pumpThread_, both of
    // which would be unsafe here since next_impl() is called by
    // PumpLoop() while already holding that same lock, on that same
    // thread. Matches the real SDK's finish_running(), including its
    // precondition ("shall only be called while the measurement mutex is
    // locked") and the fact that it does NOT call on_stop() for you --
    // call that yourself afterward if cleanup needs to run.
    //
    // Ensures no further data is delivered (PumpLoop's while(running_)
    // check will see this on its next iteration and exit), but leaves
    // pumpThread_ unjoined -- a later stop() (e.g. from ~EventPipeline()
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
        // SIMULATED_LINE_WIDTH_PIXELS must match the acquisition's configured
        // ROI width, or pixel_marker_processor will see dead time within
        // each simulated line. SIMULATED_PIXEL_PERIOD_PS is scaled down from
        // a more realistic ~10 MHz sync rate -- this pipeline can't sustain
        // the tag rate that would produce.
        constexpr timestamp_t linePeriodPs =
            SIMULATED_LINE_WIDTH_PIXELS * SIMULATED_PIXEL_PERIOD_PS;
        // Assumes a square frame (SIMULATED_LINE_WIDTH_PIXELS lines, same as
        // the line width in pixels) -- matches this fake's only supported
        // resolution (see test_acquisition.cpp's kResolution comment).
        constexpr timestamp_t framePeriodPs =
            SIMULATED_LINE_WIDTH_PIXELS * linePeriodPs;

        // elapsedPs free-runs with the wall clock; generatedPs/
        // nextFrameBoundaryPs cap how much of it any one iteration is
        // allowed to turn into tags. Without that cap, a downstream
        // consumer that falls behind real time causes every subsequent
        // iteration to see a larger elapsedS (it's been longer since the
        // last, slower, next_impl() call returned), which generates an
        // even bigger batch, taking next_impl() even longer -- an
        // unbounded feedback loop whose batches only ever grow. Capping
        // generation to one frame per iteration turns that into a bounded
        // backlog of whole frames instead, and keeps next_impl() calls
        // (and therefore how long Stop has to wait before running_ is
        // rechecked) bounded to at most one frame's worth of tags.
        double elapsedPs = 0.0;
        double generatedPs = 0.0;
        double nextFrameBoundaryPs = static_cast<double>(framePeriodPs);
        auto last = std::chrono::steady_clock::now();
        std::mt19937_64 rng{std::random_device{}()};
        std::map<channel_t, double> nextArrivalPs;
        std::optional<double> nextPhotonCandidatePs;

        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

            auto const now = std::chrono::steady_clock::now();
            double const elapsedS =
                std::chrono::duration_cast<std::chrono::duration<double>>(
                    now - last)
                    .count();
            last = now;
            elapsedPs += elapsedS * 1e12;
            double const generateUntilPs =
                std::min(elapsedPs, nextFrameBoundaryPs);
            timestamp_t const beginTime = static_cast<timestamp_t>(generatedPs);
            timestamp_t const endTime = static_cast<timestamp_t>(generateUntilPs);

            auto lock = getLock();
            if (!running_)
                break;

            std::vector<Tag> batch;

            bool const photonPosRegistered =
                std::find(registeredChannels_.begin(),
                          registeredChannels_.end(),
                          PHOTON_CHANNEL) != registeredChannels_.end();
            bool const photonNegRegistered =
                std::find(registeredChannels_.begin(),
                          registeredChannels_.end(),
                          -PHOTON_CHANNEL) != registeredChannels_.end();
            if (photonPosRegistered || photonNegRegistered) {
                // Gated Poisson photon noise correlated to the simulated
                // sync channel (see the sync/photon comment above). Both
                // polarities are generated together here, from
                // one shared draw sequence, so pair_one_between finds a
                // matching rising/falling pair for every simulated photon
                // instead of two independently drifting tag streams.
                std::exponential_distribution<double> gateDist(
                    SIMULATED_PHOTON_GATE_RATE_HZ);
                // gateDist(rng) is continuous, so it occasionally draws a
                // sub-picosecond value (P(< 1 ps) ~= rate * 1e-12 =~ 1e-4
                // for SIMULATED_PHOTON_GATE_RATE_HZ -- not rare enough to
                // ignore across the tens of thousands of draws in a typical
                // run). Any time that's used as an offset from a
                // sync-aligned reference point (a period boundary, or here,
                // time zero, where the very first SYNC_CHANNEL tick also
                // fires), static_cast<timestamp_t>'s truncation can then
                // land the resulting integer picosecond exactly on top of
                // that reference instead of strictly after it. Flooring at
                // 1 ps guarantees "after", with no meaningful effect on the
                // distribution (SIMULATED_PHOTON_GATE_RATE_HZ's ~10,000 ps
                // mean makes this floor negligible).
                auto const drawPositivePs = [&] {
                    return std::max(1.0, gateDist(rng) * 1e12);
                };
                if (!nextPhotonCandidatePs)
                    nextPhotonCandidatePs = drawPositivePs();
                for (;;) {
                    // A stop request (running_ flipped false by the
                    // consumer thread reaching its frame count, or by
                    // next_impl() itself) can land mid-generation, since
                    // this loop is the single most expensive part of a
                    // PumpLoop iteration and running_ is otherwise only
                    // checked once per iteration. Bail out immediately
                    // rather than generating a backlog's worth of tags
                    // nobody will ever consume.
                    if (!running_)
                        break;
                    double &t = *nextPhotonCandidatePs;
                    if (t >= generateUntilPs)
                        break;
                    auto const periodIndex = static_cast<std::int64_t>(
                        t / static_cast<double>(SIMULATED_PIXEL_PERIOD_PS));
                    double const periodStart =
                        static_cast<double>(periodIndex) *
                        static_cast<double>(SIMULATED_PIXEL_PERIOD_PS);
                    double const offsetInPeriod = t - periodStart;
                    if (offsetInPeriod <
                        static_cast<double>(SIMULATED_PHOTON_GATE_WIDTH_PS)) {
                        if (photonPosRegistered)
                            batch.emplace_back(static_cast<timestamp_t>(t),
                                                PHOTON_CHANNEL);
                        if (photonNegRegistered)
                            batch.emplace_back(
                                static_cast<timestamp_t>(
                                    t + SIMULATED_PHOTON_PULSE_WIDTH_PS),
                                -PHOTON_CHANNEL);
                        // Schedule the next candidate from this pulse's
                        // FALLING edge, not its rising edge -- otherwise a
                        // short draw can land the next rising edge before
                        // this pulse's falling edge, producing two rising
                        // transitions in a row on the same channel, which
                        // is not physically possible for a real detector
                        // pulse (a single digital line's edges must
                        // strictly alternate).
                        t += static_cast<double>(SIMULATED_PHOTON_PULSE_WIDTH_PS) +
                             drawPositivePs();
                    } else {
                        // Past this period's gate -- jump to the start of
                        // the next one and draw a FRESH candidate from
                        // there, rather than treating that boundary itself
                        // as a candidate. The latter was a real bug: any
                        // value that's exactly a period boundary always has
                        // offsetInPeriod == 0, which always satisfies the
                        // gate check above, so it would have deterministically
                        // produced a "detection" -- coincident with that
                        // period's sync pulse -- on every single jump,
                        // silently guaranteeing at least one photon per
                        // period instead of a true mean-~1 Poisson process
                        // with some periods empty. Drawing fresh here is
                        // also the mathematically correct way to skip dead
                        // time in a thinned Poisson process: the
                        // exponential distribution is memoryless, so
                        // resetting the draw from the new gate's start is
                        // statistically equivalent to continuing the same
                        // rate-SIMULATED_PHOTON_GATE_RATE_HZ process and just
                        // never observing it during the dead zone.
                        t = periodStart +
                            static_cast<double>(SIMULATED_PIXEL_PERIOD_PS) +
                            drawPositivePs();
                    }
                }
            }

            for (channel_t const channel : registeredChannels_) {
                // See the same check in the gated-photon loop above.
                if (!running_)
                    break;
                if (channel == PHOTON_CHANNEL ||
                    channel == -PHOTON_CHANNEL)
                    continue; // handled above, as a correlated gated pair

                // Hardcoded simulated line-clock/sync signals (see the
                // constants' own comments above); a registered channel
                // that isn't one of these produces nothing at all -- this
                // fake only models the channels this project actually
                // wires up (see the file comment for why there's no way to
                // configure a channel's simulated rate/pattern beyond this
                // fixed set).
                timestamp_t period = 0;
                timestamp_t phaseOffset = 0;
                if (channel == LINE_CLOCK_CHANNEL) {
                    period = linePeriodPs;
                } else if (channel == -LINE_CLOCK_CHANNEL) {
                    // Real line-clock hardware (e.g. OpenScan-OpenScanNIDAQ's
                    // GenerateLineClock) holds the signal high for one
                    // line's worth of active pixel-clock samples, then low
                    // until the next line's rising edge -- i.e. the falling
                    // edge trails its rising edge by width * pixel_period,
                    // same as linePeriodPs here. This fake deliberately
                    // models near-zero retrace/dead-time between lines
                    // (linePeriodPs IS that same width * pixel_period
                    // product -- see its definition above), so the falling
                    // edge lands 1 ps -- the smallest gap this fake's
                    // picosecond resolution can represent -- before the
                    // NEXT rising edge, rather than exactly coincident with
                    // it. Deliberately not coincident: two tags with
                    // identical timestamps have no guaranteed relative
                    // order once sorted (std::sort isn't stable), which
                    // used to make consumers unable to rely on seeing the
                    // falling edge before the next rising edge.
                    period = linePeriodPs;
                    phaseOffset = linePeriodPs - 1;
                } else if (channel == SYNC_CHANNEL) {
                    period = SIMULATED_PIXEL_PERIOD_PS;
                } else if (channel == -SYNC_CHANNEL) {
                    period = SIMULATED_PIXEL_PERIOD_PS;
                    phaseOffset = SIMULATED_SYNC_PULSE_WIDTH_PS;
                } else {
                    continue; // no simulated signal on this channel
                }

                auto it = nextArrivalPs.find(channel);
                if (it == nextArrivalPs.end()) {
                    it = nextArrivalPs
                             .emplace(channel, static_cast<double>(phaseOffset))
                             .first;
                }
                while (running_ && it->second < generateUntilPs) {
                    batch.emplace_back(static_cast<timestamp_t>(it->second),
                                        channel);
                    it->second += static_cast<double>(period);
                }
            }
            std::sort(batch.begin(), batch.end(),
                      [](Tag const &a, Tag const &b) { return a.time < b.time; });

            next_impl(batch, beginTime, endTime);
            generatedPs = generateUntilPs;
            bool const completedImage = generateUntilPs >= nextFrameBoundaryPs;
            if (completedImage)
                nextFrameBoundaryPs += static_cast<double>(framePeriodPs);

            // Pace the backlog: let a slow consumer fall behind by whole
            // images rather than never yielding between them. Unlock
            // first -- this sleep is real wall-clock time, and holding
            // mutex_ across it would block stop()/registerChannel() etc.
            // for the same duration.
            if (completedImage) {
                lock.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    }

    TimeTaggerBase *tagger_;
    std::mutex mutex_;
    std::atomic<bool> running_{false};
    std::thread pumpThread_;
    std::vector<channel_t> registeredChannels_;
};

// Fake-only: the one serial this fake pretends to have plugged in, and the
// model name it reports for that serial.
inline constexpr char FAKE_SERIAL[] = "SIM000001";
inline constexpr char FAKE_MODEL[] = "Simulated Time Tagger";

// Fake of createTimeTagger(): connects to the fake device if the serial
// matches (or is left empty, meaning "first available"), matching real SDK
// semantics -- empty serial connects to the first device found, a wrong
// serial throws. Does not model the `resolution` parameter or
// createTimeTaggerVirtual/TimeTaggerHardware/TimeTaggerNetwork; nothing in
// this project uses those yet (see file-level comment above).
inline TimeTaggerBase *createTimeTagger(std::string const &serial = "") {
    if (!serial.empty() && serial != FAKE_SERIAL) {
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
inline std::vector<std::string> scanTimeTagger(bool includeModelName = false) {
    if (includeModelName)
        return {std::string(FAKE_SERIAL) + "," + FAKE_MODEL};
    return {FAKE_SERIAL};
}

// Fake of getTimeTaggerModel(): reports the model for the one serial this
// fake knows about. The real SDK's doc comment doesn't specify behavior for
// an unrecognized serial; we throw, matching createTimeTagger()'s handling
// of a wrong serial above.
inline std::string getTimeTaggerModel(std::string const &serial) {
    if (serial != FAKE_SERIAL) {
        throw std::runtime_error("No Time Tagger device with serial '" +
                                  serial + "' found");
    }
    return FAKE_MODEL;
}
