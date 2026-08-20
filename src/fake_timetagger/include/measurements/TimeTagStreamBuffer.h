#pragma once

#include "TimeTagger.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

// Fake of TimeTagStreamBuffer. Mirrors the real SDK's "argout callback"
// getters: you pass a callback that allocates space and returns a pointer
// for the library to fill in.
class TimeTagStreamBuffer {
  public:
    void getTimestamps(std::function<timestamp_t *(std::size_t)> const &argout) {
        timestamp_t *dst = argout(timestamps_.size());
        std::copy(timestamps_.begin(), timestamps_.end(), dst);
    }

    void getChannels(std::function<channel_t *(std::size_t)> const &argout) {
        channel_t *dst = argout(channels_.size());
        std::copy(channels_.begin(), channels_.end(), dst);
    }

    std::uint64_t size = 0;

  private:
    friend class TimeTagStream;

    std::vector<timestamp_t> timestamps_;
    std::vector<channel_t> channels_;
};
