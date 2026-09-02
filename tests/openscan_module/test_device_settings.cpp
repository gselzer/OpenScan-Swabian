// Tests OpenScanSwabian's device-level contract (TimeTagger.cpp,
// TimeTaggerSettings.cpp) through OpenScanLib's real client API:
//
// Any SECTION here that mutates a setting restores it before returning --
// see device_test_support.hpp's Environment comment for why that matters
// (the underlying device object, and therefore its settings, persist for
// the whole test binary).

#include "device_test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace test_support;

TEST_CASE("device identity and role flags", "[device]") {
    LSMFixture fx;

    const char *name = nullptr;
    CheckOk(OSc_Device_GetName(fx.detector, &name), "GetName");
    CHECK(std::string(name).find("Swabian") != std::string::npos);

    const char *displayName = nullptr;
    CheckOk(OSc_Device_GetDisplayName(fx.detector, &displayName),
            "GetDisplayName");
    // "<model>@<name>", per OSc_Device_GetDisplayName's documented format.
    CHECK(std::string(displayName).find('@') != std::string::npos);

    bool hasClock = true, hasScanner = true, hasDetector = false;
    CheckOk(OSc_Device_HasClock(fx.detector, &hasClock), "HasClock");
    CheckOk(OSc_Device_HasScanner(fx.detector, &hasScanner), "HasScanner");
    CheckOk(OSc_Device_HasDetector(fx.detector, &hasDetector), "HasDetector");
    CHECK_FALSE(hasClock);
    CHECK_FALSE(hasScanner);
    CHECK(hasDetector);
}

TEST_CASE("device settings expose the expected names, defaults, and "
          "constraints",
          "[device][settings]") {
    LSMFixture fx;

    OSc_Setting **settings = nullptr;
    size_t count = 0;
    CheckOk(OSc_Device_GetSettings(fx.detector, &settings, &count),
            "GetSettings");
    REQUIRE(count == 12);

    SECTION("Sync Channel: default, range, and round-trip") {
        auto *s = FindSetting(settings, count, "Sync Channel");
        REQUIRE(s != nullptr);
        int32_t v = 0;
        CheckOk(OSc_Setting_GetInt32Value(s, &v), "get");
        CHECK(v == 2);

        int32_t min = 0, max = 0;
        CheckOk(OSc_Setting_GetInt32ContinuousRange(s, &min, &max), "range");
        CHECK(min == 1);
        CHECK(max == 8);

        CheckOk(OSc_Setting_SetInt32Value(s, 5), "set");
        CheckOk(OSc_Setting_GetInt32Value(s, &v), "get2");
        CHECK(v == 5);
        CheckOk(OSc_Setting_SetInt32Value(s, 2), "restore"); // leave as found
    }

    SECTION("Photon Channel and Line Clock Channel defaults") {
        auto *photon = FindSetting(settings, count, "Photon Channel");
        auto *lineClock = FindSetting(settings, count, "Line Clock Channel");
        REQUIRE(photon != nullptr);
        REQUIRE(lineClock != nullptr);

        int32_t v = 0;
        CheckOk(OSc_Setting_GetInt32Value(photon, &v), "get photon");
        CHECK(v == 3);
        CheckOk(OSc_Setting_GetInt32Value(lineClock, &v), "get line clock");
        CHECK(v == 1);
    }

    SECTION("Sync Delay: defaults to zero and round-trips") {
        auto *s = FindSetting(settings, count, "Sync Delay");
        REQUIRE(s != nullptr);
        int32_t v = -1;
        CheckOk(OSc_Setting_GetInt32Value(s, &v), "get");
        CHECK(v == 0);

        CheckOk(OSc_Setting_SetInt32Value(s, 777), "set");
        CheckOk(OSc_Setting_GetInt32Value(s, &v), "get2");
        CHECK(v == 777);
        CheckOk(OSc_Setting_SetInt32Value(s, 0), "restore");
    }

    SECTION("Line Delay: defaults to zero and only allows non-negative "
            "values") {
        auto *s = FindSetting(settings, count, "Line Delay");
        REQUIRE(s != nullptr);
        int32_t v = -1;
        CheckOk(OSc_Setting_GetInt32Value(s, &v), "get");
        CHECK(v == 0);

        int32_t min = -1, max = 0;
        CheckOk(OSc_Setting_GetInt32ContinuousRange(s, &min, &max), "range");
        CHECK(min == 0);
    }

    SECTION("Max Photon Pulse Width and Max Diff Time defaults") {
        auto *pulseWidth =
            FindSetting(settings, count, "Max Photon Pulse Width");
        auto *diffTime = FindSetting(settings, count, "Max Diff Time");
        REQUIRE(pulseWidth != nullptr);
        REQUIRE(diffTime != nullptr);

        int32_t v = 0;
        CheckOk(OSc_Setting_GetInt32Value(pulseWidth, &v), "get pulse width");
        CHECK(v == 100'000);
        CheckOk(OSc_Setting_GetInt32Value(diffTime, &v), "get diff time");
        CHECK(v == 15'000);

        int32_t min = -1, max = 0;
        CheckOk(OSc_Setting_GetInt32ContinuousRange(pulseWidth, &min, &max),
                "range pulse width");
        CHECK(min == 1); // must stay positive: a histogram bin_width divisor
    }

    SECTION("Histogram Bins: default and discrete value set") {
        auto *s = FindSetting(settings, count, "Histogram Bins");
        REQUIRE(s != nullptr);
        int32_t v = 0;
        CheckOk(OSc_Setting_GetInt32Value(s, &v), "get");
        CHECK(v == 256);

        int32_t *values = nullptr;
        size_t n = 0;
        CheckOk(OSc_Setting_GetInt32DiscreteValues(s, &values, &n),
                "discrete values");
        std::vector<int32_t> const expected = {16,  32,  64,   128, 256,
                                                512, 1024, 2048, 4096};
        std::vector<int32_t> const got(values, values + n);
        CHECK(got == expected);
    }

    SECTION("boolean settings default to false and round-trip") {
        for (std::string const &name :
             {"Save Histograms", "Cumulative", "Save Raw Data"}) {
            auto *s = FindSetting(settings, count, name);
            REQUIRE(s != nullptr);
            bool v = true;
            CheckOk(OSc_Setting_GetBoolValue(s, &v), "get " + name);
            CHECK_FALSE(v);

            CheckOk(OSc_Setting_SetBoolValue(s, true), "set " + name);
            CheckOk(OSc_Setting_GetBoolValue(s, &v), "get2 " + name);
            CHECK(v);
            CheckOk(OSc_Setting_SetBoolValue(s, false), "restore " + name);
        }
    }

    SECTION("File Name Prefix: default and round-trip") {
        auto *s = FindSetting(settings, count, "File Name Prefix");
        REQUIRE(s != nullptr);
        char buf[OSc_MAX_STR_SIZE];
        CheckOk(OSc_Setting_GetStringValue(s, buf), "get");
        CHECK(std::string(buf) == "OpenScan-Swabian");

        CheckOk(OSc_Setting_SetStringValue(s, "MyPrefix"), "set");
        CheckOk(OSc_Setting_GetStringValue(s, buf), "get2");
        CHECK(std::string(buf) == "MyPrefix");
        CheckOk(OSc_Setting_SetStringValue(s, "OpenScan-Swabian"), "restore");
    }
}
