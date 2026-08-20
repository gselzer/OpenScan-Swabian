#pragma once

#include "TimeTagger.h"

#include <cstdint>

namespace Experimental {

// Fake of Experimental::ExponentialSignalGenerator: registers a new
// simulated channel producing Poisson-arrival events at the given rate.
// Actual event generation happens lazily in TimeTagStream::getData().
class ExponentialSignalGenerator {
  public:
    ExponentialSignalGenerator(TimeTaggerBase *tagger, double rate,
                                channel_t /*base_channel*/ = 0,
                                std::int32_t /*seed*/ = -1)
        : channel_(tagger->RegisterChannel(rate)) {}

    [[nodiscard]] channel_t getChannel() const { return channel_; }

  private:
    channel_t channel_;
};

} // namespace Experimental
