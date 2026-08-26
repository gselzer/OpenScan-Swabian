#include <TimeTagger.h>
#include <measurements/SignalGenerators.h>
#include <measurements/TimeTagStream.h>
#include <measurements/TimeTagStreamBuffer.h>

#include <libtcspc/tcspc.hpp>

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <thread>
#include <vector>

int main() {
    // Create fake device
    TimeTaggerBase *tagger = createTimeTagger();

    Experimental::ExponentialSignalGenerator generator(tagger, 100'000.0);   
    channel_t channel = generator.getChannel();

    TimeTagStream stream(tagger, 1'000'000, {channel});
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    TimeTagStreamBuffer buf = stream.getData();

    std::cout << "Got " << buf.size << " events on channel " << channel << std::endl;

    freeTimeTagger(tagger);
    return 0;
}