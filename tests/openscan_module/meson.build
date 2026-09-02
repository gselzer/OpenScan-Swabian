unique_filename_test_exe = executable(
    'test_unique_filename',
    ['test_unique_filename.cpp', '../../src/UniqueFileName.cpp'],
    include_directories: include_directories('../../src'),
    dependencies: [catch2_with_main_dep],
)

test('openscan_module_unique_filename', unique_filename_test_exe, timeout: 60)

# The rest of this directory opens the real OpenScanSwabian.osdev device and
# asserts on the data it produces (e.g. "mean ~1 count/pixel"), which is
# specific to the fake Time Tagger backend's synthetic generation -- against
# real hardware, TimeTagger_Open() would try to connect to an actual
# instrument (almost certainly absent in a dev/CI environment) and the data
# assertions would be meaningless even if one were present. Only build and
# run it when the module itself was built in simulate mode.
if get_option('simulate')
    subdir('fake_clock_scanner_device')

    # The directory the built module lands in (fake_clock_scanner_module_dir
    # is set by the subdir() above, next to its own shared_module() call),
    # forward-slash-normalized so it's safe to embed as a C string literal
    # regardless of platform path separators.
    osdev_module_dir = meson.project_build_root().replace('\\', '/')

    device_module_test_srcs = [
        'test_device_settings.cpp',
        'test_acquisition.cpp',
    ]

    device_module_test_exe = executable(
        'test_openscan_module',
        device_module_test_srcs,
        cpp_args: [
            '-DOSDEV_MODULE_DIR="' + osdev_module_dir + '"',
            '-DFAKE_CLOCK_SCANNER_MODULE_DIR="' + fake_clock_scanner_module_dir + '"',
        ],
        # test_acquisition.cpp includes TimeTagger.h (the fake) only to
        # decode the raw-dump Tag layout it wrote -- see its own comment.
        include_directories: include_directories(
            '../../src/fake_timetagger/include',
        ),
        dependencies: [
            catch2_with_main_dep,
            openscanlib_dep,
        ],
    )

    test(
        'openscan_module_device_integration',
        device_module_test_exe,
        # The test binary loads OpenScanSwabian.osdev and
        # FakeClockScannerDevice.osdev by scanning the directories above at
        # runtime, so both must exist before the test runs (compiling the
        # test binary itself doesn't require it -- their paths are just
        # embedded strings).
        depends: [osdev, fake_clock_scanner_device],
        timeout: 60,
    )
else
    message('Skipping openscan_module device-integration tests: requires -Dsimulate=true')
endif
