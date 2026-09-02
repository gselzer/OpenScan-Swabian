#pragma once

// Shared fixtures for tests that drive the built OpenScanSwabian.osdev
// module through OpenScanLib's real client API (the same API a host
// application, e.g. Micro-Manager, would use) -- see test_device_settings.cpp
// and test_acquisition.cpp.
//
// OSDEV_MODULE_DIR and FAKE_CLOCK_SCANNER_MODULE_DIR are provided as
// compile definitions by meson.build, pointing at the build directories
// containing OpenScanSwabian.osdev and the test-only
// FakeClockScannerDevice.osdev respectively (see
// ./fake_clock_scanner_device/FakeClockScannerDevice.cpp for why a second,
// trivial device module is needed at all: it fills the clock and scanner
// roles that a detector-only acquisition like this one still needs
// *something* assigned to).

#include <catch2/catch_test_macros.hpp>

#include <OpenScanLib.h>

#include <string>

namespace test_support {

inline void CheckOk(OSc_RichError *err, char const *what) {
    if (!err)
        return;
    char buf[2048];
    OSc_Error_FormatRecursive(err, buf, sizeof(buf));
    OSc_Error_Destroy(err);
    INFO(what);
    FAIL(buf);
}

inline void CheckOk(OSc_RichError *err, std::string const &what) {
    CheckOk(err, what.c_str());
}

// OpenScanLib enumerates every device exactly once per process (see
// OScInternal_DeviceModule.c / DeviceEnumeration.c's g_deviceInstances) and
// hands back the *same* device objects on every subsequent call -- there is
// no API to reset or re-enumerate. So device discovery has to happen
// exactly once for this whole test binary; this wraps that one-time setup
// and the two devices tests need out of it.
struct Environment {
    OSc_Device *timeTaggerDevice = nullptr;
    OSc_Device *fakeClockScannerDevice = nullptr;

    Environment() {
        REQUIRE(OSc_CheckVersion());

        // OSc_SetDeviceModuleSearchPaths() makes its own copy of these
        // strings, so the temporaries here don't need to outlive the call.
        std::string osdevDir = OSDEV_MODULE_DIR;
        std::string clockScannerDir = FAKE_CLOCK_SCANNER_MODULE_DIR;
        char *paths[3] = {osdevDir.data(), clockScannerDir.data(), nullptr};
        OSc_SetDeviceModuleSearchPaths(paths);

        OSc_Device **devices = nullptr;
        size_t count = 0;
        CheckOk(OSc_GetAllDevices(&devices, &count), "OSc_GetAllDevices");
        REQUIRE(count == 2);

        for (size_t i = 0; i < count; ++i) {
            const char *name = nullptr;
            CheckOk(OSc_Device_GetName(devices[i], &name),
                    "OSc_Device_GetName");
            std::string const n = name ? name : "";
            if (n.find("FakeClockScanner") != std::string::npos)
                fakeClockScannerDevice = devices[i];
            else if (n.find("Swabian") != std::string::npos)
                timeTaggerDevice = devices[i];
        }
        REQUIRE(timeTaggerDevice != nullptr);
        REQUIRE(fakeClockScannerDevice != nullptr);
    }
};

inline Environment &SharedEnvironment() {
    static Environment env;
    return env;
}

// Because device enumeration only ever happens once (see Environment
// above), timeTaggerDevice/fakeClockScannerDevice are the *same* two
// objects for every test case in this binary; only the LSM they're
// associated with, and the fact that they're open/closed, is per-test.
// Settings a test mutates on the shared TimeTagger device therefore
// persist into later test cases -- see test_device_settings.cpp, which
// restores what it changes, and test_acquisition.cpp, which explicitly
// sets every setting it depends on rather than relying on ambient
// defaults.
struct LSMFixture {
    OSc_LSM *lsm = nullptr;
    OSc_Device *detector;
    OSc_Device *clockScanner;

    LSMFixture()
        : detector(SharedEnvironment().timeTaggerDevice),
          clockScanner(SharedEnvironment().fakeClockScannerDevice) {
        CheckOk(OSc_LSM_Create(&lsm), "OSc_LSM_Create");
        CheckOk(OSc_Device_Open(clockScanner, lsm),
                "OSc_Device_Open(clockScanner)");
        CheckOk(OSc_Device_Open(detector, lsm), "OSc_Device_Open(detector)");
        CheckOk(OSc_LSM_SetClockDevice(lsm, clockScanner),
                "OSc_LSM_SetClockDevice");
        CheckOk(OSc_LSM_SetScannerDevice(lsm, clockScanner),
                "OSc_LSM_SetScannerDevice");
        CheckOk(OSc_LSM_SetDetectorDevice(lsm, detector),
                "OSc_LSM_SetDetectorDevice");

        // Confirm the roles actually landed as intended -- every test using
        // this fixture depends on OpenScanSwabian specifically being the
        // detector and the fake filling the clock/scanner roles (see
        // fake_clock_scanner_device/FakeClockScannerDevice.cpp for why that
        // second device exists at all).
        REQUIRE(OSc_LSM_GetDetectorDevice(lsm) == detector);
        REQUIRE(OSc_LSM_GetClockDevice(lsm) == clockScanner);
        REQUIRE(OSc_LSM_GetScannerDevice(lsm) == clockScanner);
    }

    ~LSMFixture() {
        OSc_Device_Close(detector);
        OSc_Device_Close(clockScanner);
        OSc_LSM_Destroy(lsm);
    }

    LSMFixture(LSMFixture const &) = delete;
    LSMFixture &operator=(LSMFixture const &) = delete;
};

inline OSc_Setting *FindSetting(OSc_Setting **settings, size_t count,
                                 std::string const &name) {
    for (size_t i = 0; i < count; ++i) {
        char buf[OSc_MAX_STR_SIZE];
        CheckOk(OSc_Setting_GetName(settings[i], buf), "OSc_Setting_GetName");
        if (name == buf)
            return settings[i];
    }
    return nullptr;
}

} // namespace test_support
