#include <catch2/catch_test_macros.hpp>

#include <TimeTagger.h>

TEST_CASE("input delay round-trips per channel", "[TimeTaggerBase]") {
    TimeTaggerBase tagger;
    tagger.setInputDelay(5, 12345);
    CHECK(tagger.getInputDelay(5) == 12345);
    // A channel that was never set reads back as a default-constructed
    // timestamp_t (0), not garbage.
    CHECK(tagger.getInputDelay(6) == 0);
}

TEST_CASE("hardware delay round-trips and reports a plausible range",
          "[TimeTaggerBase]") {
    TimeTaggerBase tagger;
    tagger.setDelayHardware(5, -1000);
    CHECK(tagger.getDelayHardware(5) == -1000);

    auto const range = tagger.getDelayHardwareRange(5);
    REQUIRE(range.size() == 2);
    CHECK(range[0] < 0);
    CHECK(range[1] > 0);
}

TEST_CASE("software delay round-trips per channel", "[TimeTaggerBase]") {
    TimeTaggerBase tagger;
    tagger.setDelaySoftware(2, 42);
    CHECK(tagger.getDelaySoftware(2) == 42);
}

TEST_CASE("deadtime round-trips, returns the set value, and reports a "
          "plausible range",
          "[TimeTaggerBase]") {
    TimeTaggerBase tagger;
    CHECK(tagger.setDeadtime(3, 500) == 500);
    CHECK(tagger.getDeadtime(3) == 500);

    auto const range = tagger.getDeadtimeRange(3);
    REQUIRE(range.size() == 2);
    CHECK(range[0] == 0);
    CHECK(range[1] > 0);
}

TEST_CASE("conditional filter round-trips and can be cleared",
          "[TimeTaggerBase]") {
    TimeTaggerBase tagger;
    tagger.setConditionalFilter({1, 2}, {3, 4});
    CHECK(tagger.getConditionalFilterTrigger() == std::vector<channel_t>{1, 2});
    CHECK(tagger.getConditionalFilterFiltered() ==
          std::vector<channel_t>{3, 4});

    tagger.clearConditionalFilter();
    CHECK(tagger.getConditionalFilterTrigger().empty());
    CHECK(tagger.getConditionalFilterFiltered().empty());
}

TEST_CASE("event divider defaults to 1 and round-trips once set",
          "[TimeTaggerBase]") {
    TimeTaggerBase tagger;
    CHECK(tagger.getEventDivider(7) == 1);
    tagger.setEventDivider(7, 4);
    CHECK(tagger.getEventDivider(7) == 4);
}

TEST_CASE("overflow counters start at zero and clear correctly",
          "[TimeTaggerBase]") {
    TimeTaggerBase tagger;
    CHECK(tagger.getOverflows() == 0);
    CHECK(tagger.getOverflowsAndClear() == 0);
    tagger.clearOverflows();
    CHECK(tagger.getOverflows() == 0);
}

TEST_CASE("reference clock state reflects what was set and can be disabled",
          "[TimeTaggerBase]") {
    TimeTaggerBase tagger;
    CHECK_FALSE(tagger.getReferenceClockState().enabled);

    tagger.setReferenceClock(6, 10e6, 1e-3, 2, 1000);
    auto const state = tagger.getReferenceClockState();
    CHECK(state.enabled);
    CHECK(state.is_locked);
    CHECK(state.clock_channel == 6);
    CHECK(state.synchronization_channel == 2);
    CHECK(state.synchronization_offset == 1000);
    CHECK(state.clock_period == static_cast<timestamp_t>(1e12 / 10e6));

    tagger.disableReferenceClock();
    CHECK_FALSE(tagger.getReferenceClockState().enabled);
}

TEST_CASE("software clock state reflects what was set and can be disabled",
          "[TimeTaggerBase]") {
    TimeTaggerBase tagger;
    CHECK_FALSE(tagger.getSoftwareClockState().enabled);

    tagger.setSoftwareClock(4, 10e6, 500);
    auto const state = tagger.getSoftwareClockState();
    CHECK(state.enabled);
    CHECK(state.is_locked);
    CHECK(state.input_channel == 4);
    CHECK(state.averaging_periods == 500);
    CHECK(state.clock_period == static_cast<timestamp_t>(1e12 / 10e6));

    tagger.disableSoftwareClock();
    CHECK_FALSE(tagger.getSoftwareClockState().enabled);
}

TEST_CASE("fence counter allocates on request and can be peeked without "
          "advancing",
          "[TimeTaggerBase]") {
    TimeTaggerBase tagger;
    unsigned int const f1 = tagger.getFence();
    unsigned int const f2 = tagger.getFence();
    CHECK(f2 == f1 + 1);

    unsigned int const peeked = tagger.getFence(false);
    CHECK(peeked == f2);

    CHECK(tagger.waitForFence(f2));
    CHECK(tagger.sync());
}

TEST_CASE("misc TimeTaggerBase queries report fixed, documented behavior",
          "[TimeTaggerBase]") {
    TimeTaggerBase tagger;
    CHECK(tagger.isUnusedChannel(CHANNEL_UNUSED));
    CHECK_FALSE(tagger.isUnusedChannel(1));
    CHECK(tagger.getInvertedChannel(1) == CHANNEL_UNUSED);
    CHECK(tagger.getConfiguration() == "{}");
    CHECK(tagger.getRegistrations(1) == 0);

    CHECK(tagger.xtra_getAutoStart());
    tagger.xtra_setAutoStart(false);
    CHECK_FALSE(tagger.xtra_getAutoStart());
}

TEST_CASE("runSynchronized invokes every callback in the map exactly once",
          "[TimeTaggerBase]") {
    TimeTaggerBase tagger;

    // We don't have real IteratorBase instances handy here; the callbacks
    // never dereference their key, so any two distinct non-null addresses
    // serve as distinct map keys.
    int dummy_a = 0, dummy_b = 0;
    auto *key_a = reinterpret_cast<IteratorBase *>(&dummy_a);
    auto *key_b = reinterpret_cast<IteratorBase *>(&dummy_b);

    int calls = 0;
    TimeTaggerBase::IteratorCallbackMap callbacks;
    callbacks[key_a] = [&](IteratorBase *) { ++calls; };
    callbacks[key_b] = [&](IteratorBase *) { ++calls; };

    tagger.runSynchronized(callbacks);
    CHECK(calls == 2);
}
