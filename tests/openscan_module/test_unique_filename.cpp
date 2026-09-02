#include <catch2/catch_test_macros.hpp>

#include "UniqueFileName.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {
// Creates a fresh scratch directory for the duration of a test case and
// removes it (recursively) on destruction, so tests don't leak files into
// the real filesystem or interfere with each other.
class ScratchDir {
  public:
    ScratchDir()
        : path_(fs::temp_directory_path() /
                 fs::path("openscan-swabian-test-" +
                          std::to_string(reinterpret_cast<std::uintptr_t>(this)))) {
        fs::remove_all(path_);
        fs::create_directories(path_);
    }
    ~ScratchDir() { fs::remove_all(path_); }

    ScratchDir(ScratchDir const &) = delete;
    ScratchDir &operator=(ScratchDir const &) = delete;

    [[nodiscard]] std::string Prefix(std::string const &name) const {
        return (path_ / name).string();
    }

    void Touch(std::string const &fullPathNoExt, std::string const &ext) const {
        std::ofstream(fullPathNoExt + ext).put('x');
    }

  private:
    fs::path path_;
};
} // namespace

TEST_CASE("UniqueFileName picks _0000 in an empty directory",
          "[UniqueFileName]") {
    ScratchDir dir;
    std::string const prefix = dir.Prefix("acq");

    auto const result = UniqueFileName(prefix, {".raw"});
    REQUIRE(result.has_value());
    CHECK(*result == prefix + "_0000");
}

TEST_CASE("UniqueFileName skips indices whose file already exists",
          "[UniqueFileName]") {
    ScratchDir dir;
    std::string const prefix = dir.Prefix("acq");

    dir.Touch(prefix + "_0000", ".raw");
    dir.Touch(prefix + "_0001", ".raw");

    auto const result = UniqueFileName(prefix, {".raw"});
    REQUIRE(result.has_value());
    CHECK(*result == prefix + "_0002");
}

TEST_CASE("UniqueFileName shares one index across multiple extensions",
          "[UniqueFileName]") {
    ScratchDir dir;
    std::string const prefix = dir.Prefix("acq");

    // Only the second extension's file exists at index 0000; that should
    // still be enough to skip it, since either extension colliding counts.
    dir.Touch(prefix + "_0000", ".json");

    auto const result = UniqueFileName(prefix, {".raw", ".json"});
    REQUIRE(result.has_value());
    CHECK(*result == prefix + "_0001");
}

TEST_CASE("UniqueFileName is unaffected by an unrelated prefix",
          "[UniqueFileName]") {
    ScratchDir dir;
    dir.Touch(dir.Prefix("other") + "_0000", ".raw");

    auto const result = UniqueFileName(dir.Prefix("acq"), {".raw"});
    REQUIRE(result.has_value());
    CHECK(*result == dir.Prefix("acq") + "_0000");
}

TEST_CASE("UniqueFileName returns nullopt once all 10000 indices are taken",
          "[UniqueFileName][slow]") {
    ScratchDir dir;
    std::string const prefix = dir.Prefix("acq");
    for (int i = 0; i < 10000; ++i) {
        char suffix[8];
        std::snprintf(suffix, sizeof(suffix), "_%04d", i);
        dir.Touch(prefix + suffix, ".raw");
    }

    auto const result = UniqueFileName(prefix, {".raw"});
    CHECK_FALSE(result.has_value());
}
