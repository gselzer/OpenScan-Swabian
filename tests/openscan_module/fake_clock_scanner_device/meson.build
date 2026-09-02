fake_clock_scanner_device = shared_module(
    'FakeClockScannerDevice',
    'FakeClockScannerDevice.cpp',
    name_suffix: 'osdev',
    cpp_args: ['-D_CRT_SECURE_NO_WARNINGS'],
    dependencies: [openscandevicelib_dep],
)

# Forward-slash-normalized so it's safe to embed as a C string literal
# regardless of platform path separators. Must be captured here, not in the
# parent meson.build -- meson.current_build_dir() is relative to whichever
# meson.build calls it, and shared_module() output always lands in that same
# directory.
fake_clock_scanner_module_dir = meson.current_build_dir().replace('\\', '/')
