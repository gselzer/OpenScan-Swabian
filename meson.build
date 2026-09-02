project(
    'OpenScanSwabian',
    'cpp',
    version: '0.1.0',
    meson_version: '>=1.9.0',
    default_options: [
        'cpp_std=c++20',
        'warning_level=3',
    ]
)

cxx = meson.get_compiler('cpp')

if cxx.get_id() != 'msvc' and cxx.get_id() != 'clang-cl'
    error('Unsupported compiler (requires msvc or clang-cl)')
endif

if get_option('simulate')
    # A header-only fake of the vendor SDK (see src/fake_timetagger/), put
    # ahead of the real SDK on the include path so `#include <TimeTagger.h>`
    # resolves to ours instead. No library to link -- it's pure C++.
    time_tagger_dep = declare_dependency(
        include_directories: include_directories('src/fake_timetagger/include'),
    )
else
    time_tagger_dir = 'C:/Program Files/Swabian Instruments/Time Tagger/driver'
    host_cpu = host_machine.cpu_family()
    if host_cpu == 'x86_64'
        time_tagger_suffix = 'x64'
    elif host_cpu == 'x86'
        time_tagger_suffix = 'x86'
    elif host_cpu == 'aarch64'
        time_tagger_suffix = 'arm64'
    else
        error(f'Unsupported host CPU family: @host_cpu@')
    endif

    # Note that the TimeTagger vendor header selects between the release/debug header itself
    # via `#pragma comment(lib, ...)`, based on whether we are in a debug build.
    # We need to match that choice.
    buildtype = get_option('buildtype')
    time_tagger_lib_name =  (buildtype == 'debug' or buildtype == 'debugoptimized') ? 'TimeTaggerD' : 'TimeTagger'

    time_tagger_inc = include_directories(time_tagger_dir / 'include')
    time_tagger_dep = declare_dependency(
        dependencies: cxx.find_library(
            time_tagger_lib_name,
            dirs: time_tagger_dir / time_tagger_suffix,
            has_headers: 'TimeTagger.h',
            header_include_directories: time_tagger_inc,
        ),
        include_directories: time_tagger_inc,
    )
endif


libtcspc_dep = dependency(
    'libtcspc',
    fallback: ['libtcspc', 'libtcspc_dep'],
    default_options: [
        'docs=disabled',
        'python_docs=disabled',
        'tests=disabled',
        'benchmarks=disabled',
        'examples=disabled',
    ]
)

openscandevicelib_dep = dependency(
    'OpenScanDeviceLib',
    fallback: ['OpenScanLib', 'OpenScanDeviceLib'],
    static: true,
    default_options: [
        'devicelib=enabled',
        # apilib is needed too: our own tests/ drive the built module
        # through the real OpenScanLib client API (see tests/openscan_module).
        'apilib=enabled',
        'docs=disabled',
        'tests=disabled',
    ]
)

# Only used by our own tests (tests/openscan_module) to load and exercise the
# module below the same way a real host application would. Reuses whatever
# OpenScanLib subproject configuration openscandevicelib_dep above already
# triggered (its default_options are only honored on that first dependency()
# call for a given subproject).
openscanlib_dep = dependency(
    'OpenScanLib',
    fallback: ['OpenScanLib', 'openscan_dep'],
)

src = files(
    'src/EventPipeline.cpp',
    'src/TimeTagger.cpp',
    'src/TimeTaggerSettings.cpp',
    'src/UniqueFileName.cpp',
)

osdev = shared_module(
    'OpenScanSwabian',
    [
        src,
    ],
    name_suffix: 'osdev',
    include_directories: [
        include_directories('src'),
        include_directories('src/fake_timetagger'),
    ],
    dependencies: [
        libtcspc_dep,
        openscandevicelib_dep,
        time_tagger_dep,
    ],
)

if get_option('tests').enabled()
    catch2_with_main_dep = dependency(
        'catch2-with-main',
        allow_fallback: true,
        required: get_option('tests'),
        disabler: true,
        include_type: 'system',
        # Otherwise Catch2's own meson_options.txt defaults 'tests' to
        # true, and its ~500-case internal self-test suite (which tests
        # Catch2 itself, not this project) gets built and registered
        # alongside ours in `meson test`.
        default_options: ['tests=false'],
    )

    subdir('tests')
endif