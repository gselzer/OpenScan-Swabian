#pragma once

// A fake of Swabian Instruments' Dump measurement
// (driver/include/measurements/Dump.h): writes the raw tag stream to a
// file as back-to-back 16-byte Tag records, with no file header --
// matching the real class's "simple uncompressed binary format" exactly
// (see tools/dump_tags.py, which reads this same format).
//
// Deliberately NOT modeled: the empty-channels-means-"all active
// channels" convenience the real constructor offers. TimeTaggerBase has
// no channel enumeration in this fake (getChannelList() is
// TimeTaggerHardware-only, out of scope -- see TimeTagger.h's file-level
// comment), so there is nothing to resolve "all channels" against; pass
// the channels you want explicitly.
//
// Also NOT modeled: stopping the iterator itself once max_tags is
// reached. next_impl() runs inside IteratorBase's pump loop while its
// mutex is already held, so calling stop() from within it here would
// deadlock on that same (non-recursive) lock. Instead, writing simply
// stops at the limit; the iterator keeps running (idle) until something
// else calls stop().

#include "TimeTagger.h"

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

class Dump : public IteratorBase {
  public:
    Dump(TimeTaggerBase *tagger, std::string filename, std::int64_t max_tags,
         std::vector<channel_t> channels = std::vector<channel_t>())
        : IteratorBase(tagger), max_tags_(max_tags) {
        for (channel_t const channel : channels)
            registerChannel(channel);

        file_.open(filename, std::ios::binary | std::ios::trunc);
        if (!file_.is_open()) {
            throw std::runtime_error("Dump: could not open file '" +
                                      filename + "'");
        }

        finishInitialization();
    }

    ~Dump() override { stop(); }

  protected:
    bool next_impl(std::vector<Tag> &incoming_tags, timestamp_t /*begin_time*/,
                    timestamp_t /*end_time*/) override {
        for (Tag const &tag : incoming_tags) {
            if (max_tags_ >= 0 && tags_written_ >= max_tags_)
                break;
            file_.write(reinterpret_cast<char const *>(&tag), sizeof(Tag));
            ++tags_written_;
        }
        return false;
    }

    void clear_impl() override { tags_written_ = 0; }
    void on_start() override {}
    void on_stop() override { file_.flush(); }

  private:
    std::int64_t max_tags_;
    std::int64_t tags_written_ = 0;
    std::ofstream file_;
};
