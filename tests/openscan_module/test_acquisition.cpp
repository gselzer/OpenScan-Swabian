// End-to-end acquisition test: drives the built OpenScanSwabian.osdev
// module through OpenScanLib's real client API (LSM -> AcqTemplate ->
// Acquisition -> Arm/Start/Wait) exactly as a host application would, and
// checks that a real frame of "reasonable" data comes back.

#include "device_test_support.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// Only for the Tag struct/LineClockChannel constant, to decode the raw dump
// byte-for-byte in the same layout EventPipeline.cpp wrote it -- this test
// otherwise only drives the module through the public OpenScanLib API, like
// every other test in this directory.
#include <TimeTagger.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <numeric>
#include <vector>

using namespace test_support;

namespace {

// The fake Time Tagger's simulated line-clock period is hardcoded to
// kSimulatedLineWidthPixels (512) * pixel period (see
// src/fake_timetagger/include/TimeTagger.h's PumpLoop) -- a different ROI
// here would open pixel windows covering only part of each simulated line,
// silently dropping the rest of that line's photons as "dead time", so
// every acquisition in this file uses this fixed resolution.
constexpr uint32_t kResolution = 512;

// Retains each captured frame. Assumes each is the same size.
struct FrameCapture {
    std::vector<std::vector<std::uint16_t>> frames;
    uint32_t channel = 0xFFFFFFFF;
    uint32_t width = 0;
    uint32_t height = 0;
};

bool OnFrame(OSc_Acquisition *, uint32_t channel, void *pixelData,
             void *data) {
    auto *capture = static_cast<FrameCapture *>(data);
    capture->channel = channel;
    auto const *src = static_cast<std::uint16_t const *>(pixelData);
    capture->frames.emplace_back(
        src, src + static_cast<size_t>(capture->width) * capture->height);
    return true;
}

// Everything a test needs to inspect or clean up after RunAcquisition().
// tmpl/acq are left alive (not destroyed) so callers can do their own
// post-wait checks (e.g. OSc_Acquisition_Get*) or call
// OSc_Acquisition_Stop() before tearing them down -- see that function's
// own comment for why Stop() is sometimes required first. settings/
// settingCount are also kept around so callers can restore whatever they
// mutated via additionalSetup (see device_test_support.hpp's Environment
// comment on why that matters).
struct AcquisitionSetup {
    OSc_Setting **settings = nullptr;
    size_t settingCount = 0;
    OSc_AcqTemplate *tmpl = nullptr;
    OSc_Acquisition *acq = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
};

// Sets up and runs a kResolution x kResolution @ 1MHz OpenScan acquisition
// of numberOfFrames frames, using the Swabian Time Tagger as the detector
// and the fake clock/scanner device as the clock/scanner.
//
// Caller owns cleanup: destroy the returned acq and tmpl when done (via
// OSc_Acquisition_Stop() first if the pipeline needs to be flushed).
AcquisitionSetup RunAcquisition(
    LSMFixture &fx, uint32_t numberOfFrames, bool cumulative,
    OSc_FrameCallback callback, void *data,
    std::function<void(OSc_Setting **, size_t)> const &additionalSetup = {}) {
    AcquisitionSetup run;
    CheckOk(OSc_Device_GetSettings(fx.detector, &run.settings,
                                   &run.settingCount),
            "GetSettings");
    CheckOk(OSc_Setting_SetInt32Value(
                FindSetting(run.settings, run.settingCount, "Sync Channel"),
                2),
            "set Sync Channel");
    CheckOk(OSc_Setting_SetInt32Value(
                FindSetting(run.settings, run.settingCount, "Photon Channel"),
                3),
            "set Photon Channel");
    CheckOk(OSc_Setting_SetInt32Value(
                FindSetting(run.settings, run.settingCount,
                            "Line Clock Channel"),
                1),
            "set Line Clock Channel");
    CheckOk(OSc_Setting_SetBoolValue(
                FindSetting(run.settings, run.settingCount, "Cumulative"),
                cumulative),
            "set Cumulative");
    if (additionalSetup)
        additionalSetup(run.settings, run.settingCount);

    CheckOk(OSc_AcqTemplate_Create(&run.tmpl, fx.lsm), "AcqTemplate_Create");

    OSc_Setting *pixelRateSetting = nullptr;
    CheckOk(OSc_AcqTemplate_GetPixelRateSetting(run.tmpl, &pixelRateSetting),
            "GetPixelRateSetting");
    CheckOk(OSc_Setting_SetFloat64Value(pixelRateSetting, 1e6),
            "set pixel rate");

    OSc_Setting *resolutionSetting = nullptr;
    CheckOk(OSc_AcqTemplate_GetResolutionSetting(run.tmpl,
                                                 &resolutionSetting),
            "GetResolutionSetting");
    CheckOk(OSc_Setting_SetInt32Value(resolutionSetting,
                                      static_cast<int32_t>(kResolution)),
            "set resolution");

    uint32_t xOff = 0, yOff = 0;
    CheckOk(OSc_AcqTemplate_GetROI(run.tmpl, &xOff, &yOff, &run.width,
                                   &run.height),
            "GetROI");
    CheckOk(OSc_AcqTemplate_SetNumberOfFrames(run.tmpl, numberOfFrames),
            "SetNumberOfFrames");

    CheckOk(OSc_Acquisition_Create(&run.acq, run.tmpl), "Acquisition_Create");

    CheckOk(OSc_Acquisition_SetData(run.acq, data), "SetData");
    CheckOk(OSc_Acquisition_SetFrameCallback(run.acq, callback),
            "SetFrameCallback");

    CheckOk(OSc_Acquisition_Arm(run.acq), "Acquisition_Arm");
    CheckOk(OSc_Acquisition_Start(run.acq), "Acquisition_Start");
    CheckOk(OSc_Acquisition_Wait(run.acq), "Acquisition_Wait");

    return run;
}

} // namespace

TEST_CASE("a single-frame acquisition produces a plausible intensity image",
          "[acquisition][slow]") {

    LSMFixture fx;
    FrameCapture capture;
    capture.width = kResolution;
    capture.height = kResolution;
    AcquisitionSetup run = RunAcquisition(fx, 1, false, OnFrame, &capture);

    REQUIRE(run.width == kResolution);
    REQUIRE(run.height == kResolution);
    REQUIRE(capture.frames.size() == 1);
    CHECK(capture.channel == 0);

    // Assert only one channel in the output
    uint32_t numChannels = 0;
    CheckOk(OSc_Acquisition_GetNumberOfChannels(run.acq, &numChannels),
            "GetNumberOfChannels");
    REQUIRE(numChannels == 1);

    // Assert noise-like qualities on the image
    auto const &px = capture.frames.front();
    double const mean =
        std::accumulate(px.begin(), px.end(), 0.0) /
        static_cast<double>(px.size());
    CHECK(mean > 0);

    double const variance =
        std::accumulate(px.begin(), px.end(), 0.0,
                        [mean](double acc, auto v) {
                            double const d = v - mean;
                            return acc + d * d;
                        }) /
        static_cast<double>(px.size());
    CHECK(std::sqrt(variance) > 0.5);

    // Cleanup
    OSc_Acquisition_Destroy(run.acq);
    OSc_AcqTemplate_Destroy(run.tmpl);
}

TEST_CASE("Cumulative mode delivers one image per frame, each the running "
          "sum of every frame so far",
          "[acquisition][slow]") {
    LSMFixture fx;

    constexpr uint32_t kNumFrames = 3;

    FrameCapture capture;
    capture.width = kResolution;
    capture.height = kResolution;
    AcquisitionSetup run =
        RunAcquisition(fx, kNumFrames, true, OnFrame, &capture);

    REQUIRE(run.width == kResolution);
    REQUIRE(run.height == kResolution);
    REQUIRE(capture.frames.size() == kNumFrames);

    // Cumulative mode never resets or subtracts, so every pixel can only
    // grow or stay the same from one delivered frame to the next -- i.e.
    // frame N is frame N-1 plus that frame's own new (random) counts.
    for (size_t f = 1; f < capture.frames.size(); ++f) {
        auto const &prev = capture.frames[f - 1];
        auto const &cur = capture.frames[f];
        REQUIRE(prev.size() == cur.size());
        CHECK(std::equal(prev.begin(), prev.end(), cur.begin(),
                         std::less_equal<>()));
    }

    OSc_Acquisition_Destroy(run.acq);
    OSc_AcqTemplate_Destroy(run.tmpl);

    // Restore what this test mutated on the shared device (see
    // device_test_support.hpp's Environment comment).
    CheckOk(OSc_Setting_SetBoolValue(
                FindSetting(run.settings, run.settingCount, "Cumulative"),
                false),
            "restore Cumulative");
}

TEST_CASE("Save Raw Data writes every registered channel, including the "
          "line clock's falling edge, to a .raw dump",
          "[acquisition][slow]") {
    LSMFixture fx;

    // Route the dump into a scratch directory so we know exactly where to
    // find (and can clean up) the resulting file
    std::filesystem::path const scratch_dir =
        std::filesystem::temp_directory_path() /
        "openscan-swabian-raw-dump-test";
    std::filesystem::remove_all(scratch_dir);
    std::filesystem::create_directories(scratch_dir);
    std::string const prefix = (scratch_dir / "acq").string();

    // This test only inspects the raw dump file, not the delivered image,
    // so the capture just needs to exist as a valid callback target.
    FrameCapture capture;
    capture.width = kResolution;
    capture.height = kResolution;
    AcquisitionSetup run = RunAcquisition(
        fx, 1, false, OnFrame, &capture,
        [&](OSc_Setting **settings, size_t settingCount) {
            CheckOk(OSc_Setting_SetStringValue(
                        FindSetting(settings, settingCount,
                                    "File Name Prefix"),
                        prefix.c_str()),
                    "set File Name Prefix");
            CheckOk(OSc_Setting_SetBoolValue(
                        FindSetting(settings, settingCount, "Save Raw Data"),
                        true),
                    "set Save Raw Data");
        });

    // Reaching the requested frame count only calls finish_running()
    // (IteratorBase stops polling for more data); it deliberately does NOT
    // flush the pipeline -- see IteratorBase::finish_running()'s own
    // comment in TimeTagger.h. RawTagDumpSink's file is only closed once
    // something actually stops the acquisition (TimeTagger.cpp's Stop()
    // resets the pipeline, which flushes it in ~EventPipeline()), so this
    // is required here, not just tidiness -- without it the file below is
    // still open when we try to read/delete it.
    CheckOk(OSc_Acquisition_Stop(run.acq), "Acquisition_Stop");

    OSc_Acquisition_Destroy(run.acq);
    OSc_AcqTemplate_Destroy(run.tmpl);

    // UniqueFileName() (src/UniqueFileName.cpp) picks "<prefix>_0000.raw"
    // for the first file at a fresh prefix -- this scratch directory was
    // just created above, so that's the only file that can exist here.
    std::filesystem::path const raw_file = prefix + "_0000.raw";
    REQUIRE(std::filesystem::exists(raw_file));

    std::ifstream file(raw_file, std::ios::binary);
    REQUIRE(file.good());
    bool sawRisingLineClock = false;
    bool sawFallingLineClock = false;
    bool sawRisingSync = false;
    bool sawFallingSync = false;
    bool sawRisingPhoton = false;
    bool sawFallingPhoton = false;
    Tag tag;
    while (file.read(reinterpret_cast<char *>(&tag), sizeof(Tag))) {
        if (tag.channel == LineClockChannel)
            sawRisingLineClock = true;
        else if (tag.channel == -LineClockChannel)
            sawFallingLineClock = true;
        if (tag.channel == SyncChannel)
            sawRisingSync = true;
        else if (tag.channel == -SyncChannel)
            sawFallingSync = true;
        if (tag.channel == PhotonChannel)
            sawRisingPhoton = true;
        else if (tag.channel == -PhotonChannel)
            sawFallingPhoton = true;
    }
    CHECK(sawRisingLineClock);
    CHECK(sawFallingLineClock);
    CHECK(sawRisingSync);
    CHECK(sawFallingSync);
    CHECK(sawRisingPhoton);
    CHECK(sawFallingPhoton);
    file.close();
    std::filesystem::remove_all(scratch_dir);

    // Restore what this test mutated on the shared device (see
    // device_test_support.hpp's Environment comment) -- otherwise these
    // stick around for every later test case in this binary,
    // test_device_settings.cpp's default-value checks included.
    CheckOk(OSc_Setting_SetBoolValue(
                FindSetting(run.settings, run.settingCount, "Save Raw Data"),
                false),
            "restore Save Raw Data");
    CheckOk(OSc_Setting_SetStringValue(
                FindSetting(run.settings, run.settingCount,
                            "File Name Prefix"),
                "OpenScan-Swabian"),
            "restore File Name Prefix");
}
