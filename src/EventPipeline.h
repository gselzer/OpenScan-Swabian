#include <OpenScanDeviceLib.h>
#include <libtcspc/tcspc.hpp>
#include <TimeTagger.h>
#include <TimeTaggerPrivate.h>

#include <memory>
#include <optional>
#include <vector>

using abstime_type = tcspc::default_numeric_traits::abstime_type;
using difftime_type = tcspc::default_numeric_traits::difftime_type;
using channel_type = tcspc::default_numeric_traits::channel_type;
using bin_index_type = tcspc::default_numeric_traits::bin_index_type;

struct pixel_start_event {
    tcspc::i64 abstime;
};

struct pixel_stop_event {
    tcspc::i64 abstime;
};

struct pixel_tick_event {
    tcspc::i64 abstime;
};

class EventPipeline : public IteratorBase {
public:
    EventPipeline(OScDev_Device *device, OScDev_Acquisition *acq, std::shared_ptr<tcspc::context> const &ctx);
    ~EventPipeline();
protected:

    bool next_impl(std::vector<Tag> &incoming_tags, timestamp_t begin_time, timestamp_t end_time) override;
    void clear_impl() override final;
    void on_start() override;
    void on_stop() override;

private:
    std::optional<abstime_type> lastEnd_;
    OScDev_Device *device_;
    tcspc::type_erased_processor<tcspc::type_list<tcspc::swabian_tag_event>> pipeline_;
    tcspc::buffer_accessor accessor_;
    std::thread consumer_thread_;

    void PumpConsumerLoop();
};

auto make_processor(TimeTagger_PrivateData *data, OScDev_Acquisition *acq, std::shared_ptr<tcspc::context> const &ctx);