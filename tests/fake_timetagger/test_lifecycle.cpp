// Tests for the fake's connection functions (createTimeTagger,
// scanTimeTagger, getTimeTaggerModel) and for IteratorBase's start/stop/
// abort/waitUntilFinished lifecycle -- independent of what data actually
// gets generated (see test_data_generation.cpp for that).

#include <catch2/catch_test_macros.hpp>

#include "collecting_iterator.hpp"

#include <TimeTagger.h>

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

TEST_CASE("scanTimeTagger reports exactly the one fake device",
          "[connection]") {
    auto const serials = scanTimeTagger(false);
    REQUIRE(serials.size() == 1);
    CHECK(serials[0] == FAKE_SERIAL);

    auto const withModel = scanTimeTagger(true);
    REQUIRE(withModel.size() == 1);
    CHECK(withModel[0] == std::string(FAKE_SERIAL) + "," + FAKE_MODEL);
}

TEST_CASE("getTimeTaggerModel resolves the fake serial and rejects others",
          "[connection]") {
    CHECK(getTimeTaggerModel(FAKE_SERIAL) == FAKE_MODEL);
    CHECK_THROWS_AS(getTimeTaggerModel("not-a-real-serial"),
                     std::runtime_error);
}

TEST_CASE(
    "createTimeTagger accepts an empty or matching serial, rejects others",
    "[connection]") {
    TimeTaggerBase *empty = createTimeTagger("");
    REQUIRE(empty != nullptr);
    freeTimeTagger(empty);

    TimeTaggerBase *matching = createTimeTagger(FAKE_SERIAL);
    REQUIRE(matching != nullptr);
    freeTimeTagger(matching);

    CHECK_THROWS_AS(createTimeTagger("not-a-real-serial"), std::runtime_error);
}

TEST_CASE("IteratorBase start/stop/abort lifecycle", "[lifecycle]") {
    TimeTaggerBase tagger;
    CollectingIterator iter(&tagger);
    iter.registerChannel(LINE_CLOCK_CHANNEL);

    CHECK_FALSE(iter.isRunning());

    iter.start();
    CHECK(iter.isRunning());

    // start() is idempotent while already running (guarded by
    // running_.exchange in TimeTagger.h).
    iter.start();
    CHECK(iter.isRunning());

    iter.stop();
    CHECK_FALSE(iter.isRunning());

    // stop() is documented as idempotent (guards on pumpThread_.joinable()).
    iter.stop();
    CHECK_FALSE(iter.isRunning());

    // Restarting after a stop works.
    iter.start();
    CHECK(iter.isRunning());

    iter.abort();
    CHECK_FALSE(iter.isRunning());
    CHECK(iter.Snapshot().empty());
}

TEST_CASE("waitUntilFinished times out while running and returns promptly "
          "once stopped",
          "[lifecycle]") {
    TimeTaggerBase tagger;
    CollectingIterator iter(&tagger);
    iter.registerChannel(LINE_CLOCK_CHANNEL);
    iter.start();

    // Nothing will stop the acquisition on its own, so a short timeout must
    // elapse and report failure rather than hang.
    CHECK_FALSE(iter.waitUntilFinished(10));

    iter.stop();
    CHECK(iter.waitUntilFinished(10));
}
