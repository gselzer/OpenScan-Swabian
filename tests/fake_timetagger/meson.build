fake_timetagger_test_srcs = [
    'test_timetagger_base.cpp',
    'test_lifecycle.cpp',
    'test_data_generation.cpp',
]

fake_timetagger_test_exe = executable(
    'test_fake_timetagger',
    fake_timetagger_test_srcs,
    include_directories: include_directories('../../src/fake_timetagger/include'),
    dependencies: [
        catch2_with_main_dep,
    ],
)

test(
    'fake_timetagger',
    fake_timetagger_test_exe,
    timeout: 60,
)
