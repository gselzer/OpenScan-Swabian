// Tests that the fake's synthetic event generation (IteratorBase::PumpLoop)
// produces "reasonable" data: the right shape of data on the three
// hardcoded special channels (line clock, sync, gated photon noise), and
// nothing at all on any other channel -- see TimeTagger.h's file comment
// for the design this pins down.
//
// PumpLoop runs on a real background thread, sleeping ~1ms per iteration
// and pacing generation off std::chrono::steady_clock -- these tests
// therefore run for real (short) wall-clock durations and check the
// resulting data statistically, with tolerances generous enough not to
// flake under normal scheduling jitter.

#include <catch2/catch_test_macros.hpp>

#include "collecting_iterator.hpp"

#include <TimeTagger.h>

#include <chrono>
#include <optional>
#include <thread>
#include <vector>

namespace {
constexpr timestamp_t kLinePeriodPs =
    SIMULATED_LINE_WIDTH_PIXELS * SIMULATED_PIXEL_PERIOD_PS;

std::vector<Tag> Collect(std::vector<channel_t> const &channels,
                          std::chrono::milliseconds duration) {
    TimeTaggerBase tagger;
    CollectingIterator iter(&tagger);
    for (channel_t const ch : channels)
        iter.registerChannel(ch);
    iter.start();
    std::this_thread::sleep_for(duration);
    iter.stop();
    return iter.Snapshot();
}
} // namespace

TEST_CASE("LINE_CLOCK_CHANNEL emits alternating edges with periodic rising "
          "edges",
          "[data]") {
    auto const tags = Collect({LINE_CLOCK_CHANNEL, -LINE_CLOCK_CHANNEL},
                               std::chrono::milliseconds(150));
    REQUIRE(tags.size() > 1);

    // Ensure we start with a rising edge and alternate from there
    REQUIRE(tags.front().channel == LINE_CLOCK_CHANNEL);
    for (size_t i = 0; i < tags.size(); ++i) {
        REQUIRE((tags[i].channel == LINE_CLOCK_CHANNEL ||
                 tags[i].channel == -LINE_CLOCK_CHANNEL));
        CHECK(tags[i].type == Tag::Type::TimeTag);
        if (i > 0) {
            CHECK(tags[i].channel == -1 * tags[i - 1].channel); // alternation
            CHECK(tags[i].time > tags[i - 1].time); // strictly increasing
        }
    }

    std::optional<timestamp_t> last_rising_time;
    for (auto const &t : tags) {
        // Ensure rising edges are periodic
        if (t.channel == LINE_CLOCK_CHANNEL) {
            if (last_rising_time)
                CHECK(t.time - *last_rising_time == kLinePeriodPs);
            last_rising_time = t.time;
        }
    }
}

TEST_CASE("SYNC_CHANNEL emits alternating edges with a positive pulse "
          "width and periodic rising edges",
          "[data]") {
    auto const tags =
        Collect({SYNC_CHANNEL, -SYNC_CHANNEL}, std::chrono::milliseconds(20));
    REQUIRE(tags.size() > 1);

    // Ensure we start with a rising edge and alternate from there
    REQUIRE(tags.front().channel == SYNC_CHANNEL);
    for (size_t i = 0; i < tags.size(); ++i) {
        REQUIRE((tags[i].channel == SYNC_CHANNEL ||
                 tags[i].channel == -SYNC_CHANNEL));
        if (i > 0)
            CHECK(tags[i].channel == -1 * tags[i - 1].channel); // alternation
    }

    std::optional<timestamp_t> last_rising_time;
    for (size_t i = 0; i < tags.size(); ++i) {
        // Ensure laser pulses are periodic
        if (tags[i].channel == SYNC_CHANNEL) {
            if (last_rising_time)
                CHECK(tags[i].time - *last_rising_time ==
                      SIMULATED_PIXEL_PERIOD_PS);
            last_rising_time = tags[i].time;
        }
        // Ensure the pulse width is positive
        else {
            CHECK(tags[i].time > tags[i - 1].time); // positive pulse width
        }
    }
}

TEST_CASE("PHOTON_CHANNEL emits alternating edges with a positive pulse "
          "width",
          "[data]") {
    auto const tags = Collect({PHOTON_CHANNEL, -PHOTON_CHANNEL},
                               std::chrono::milliseconds(50));
    REQUIRE(tags.size() > 1);

    // Ensure we start with a rising edge and alternate from there
    REQUIRE(tags.front().channel == PHOTON_CHANNEL);
    for (size_t i = 0; i < tags.size(); ++i) {
        REQUIRE((tags[i].channel == PHOTON_CHANNEL ||
                 tags[i].channel == -PHOTON_CHANNEL));
        if (i > 0)
            CHECK(tags[i].channel == -1 * tags[i - 1].channel); // alternation
    }

    for (size_t i = 0; i < tags.size(); ++i) {
        if (tags[i].channel == -PHOTON_CHANNEL)
            CHECK(tags[i].time > tags[i - 1].time); // positive pulse width
    }
}

// Note that this test is checking an invariant, which may be trivial,
// but we'd want to know if it ever broke.
TEST_CASE("Every photon detection follows, and is close to, the most "
          "recent sync pulse",
          "[data]") {
    auto const tags = Collect({SYNC_CHANNEL, PHOTON_CHANNEL},
                               std::chrono::milliseconds(50));
    REQUIRE(tags.size() > 0);

    // The current implementation of the fake_timetagger generates photons
    // first, then sync pulses. We might stop the collection after generating the photons
    // for a pixel but before the sync pulse for that pixel, so the last sync pulse may
    // be missing from the collection -- only check photons up to that point.
    timestamp_t last_overall_sync_time = -1;
    for (auto const &t : tags)
        if (t.channel == SYNC_CHANNEL)
            last_overall_sync_time = t.time;
    REQUIRE(last_overall_sync_time >= 0);

    std::optional<timestamp_t> last_sync_time;
    for (auto const &t : tags) {
        REQUIRE((t.channel == SYNC_CHANNEL || t.channel == PHOTON_CHANNEL));
        if (t.channel == SYNC_CHANNEL) {
            last_sync_time = t.time;
            continue;
        }
        // Ignore photons that occur after the last sync pulse
        if (t.time > last_overall_sync_time)
            continue;
        REQUIRE(last_sync_time.has_value()); // no photon before the first sync pulse
        CHECK(t.time > *last_sync_time); // causality
        CHECK(t.time - *last_sync_time < SIMULATED_PIXEL_PERIOD_PS); // not absurdly late
    }
}

TEST_CASE("Tags across simultaneously-registered channels are delivered in "
          "non-decreasing time order",
          "[data]") {
    auto const tags =
        Collect({LINE_CLOCK_CHANNEL, -LINE_CLOCK_CHANNEL, SYNC_CHANNEL, -SYNC_CHANNEL, PHOTON_CHANNEL,
                  -PHOTON_CHANNEL, 99},
                std::chrono::milliseconds(30));
    REQUIRE(tags.size() > 10);
    for (size_t i = 1; i < tags.size(); ++i)
        CHECK(tags[i].time >= tags[i - 1].time);
}
