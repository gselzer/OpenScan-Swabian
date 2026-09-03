#include "UniqueFileName.h"

#include <cstdio>
#include <filesystem>

std::optional<std::string>
UniqueFileName(std::string const &prefix,
               std::vector<std::string> const &extensions) {
    for (int i = 0; i < 10000; ++i) {
        char suffix[8];
        std::snprintf(suffix, sizeof(suffix), "_%04d", i);
        std::string const candidate = prefix + suffix;

        bool anyExists = false;
        for (auto const &ext : extensions) {
            if (std::filesystem::exists(candidate + ext)) {
                anyExists = true;
                break;
            }
        }
        if (!anyExists)
            return candidate;
    }
    return std::nullopt;
}
