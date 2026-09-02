#pragma once

#include <TimeTagger.h>

#include <mutex>
#include <vector>

// A minimal concrete IteratorBase that just accumulates every tag batch
// PumpLoop hands it 
class CollectingIterator : public IteratorBase {
  public:
    explicit CollectingIterator(TimeTaggerBase *tagger) : IteratorBase(tagger) {}
    ~CollectingIterator() override { stop(); }

    using IteratorBase::registerChannel;
    using IteratorBase::unregisterChannel;

    [[nodiscard]] std::vector<Tag> Snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return tags_;
    }

  protected:
    bool next_impl(std::vector<Tag> &incoming_tags, timestamp_t /*begin_time*/,
                    timestamp_t /*end_time*/) override {
        std::lock_guard<std::mutex> lock(mutex_);
        tags_.insert(tags_.end(), incoming_tags.begin(), incoming_tags.end());
        return false;
    }

  private:
    mutable std::mutex mutex_;
    std::vector<Tag> tags_;
};
