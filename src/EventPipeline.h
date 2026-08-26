#include <OpenScanDeviceLib.h>
#include <libtcspc/tcspc.hpp>
#include <TimeTagger.h>
#include <TimeTaggerPrivate.h>

#include <memory>
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

// // Workaround for https://github.com/llvm/llvm-project/issues/54668 (probably
// // fixed in LLVM 18):
// // NOLINTNEXTLINE(bugprone-exception-escape)
// struct settings {
//     std::string output_filename;
//     channel_type sync_channel = 3;
//     channel_type pixel_marker_channel = 2;
//     channel_type photon_leading_channel = 1;
//     channel_type photon_trailing_channel = -1;
//     abstime_type sync_delay = 0;
//     abstime_type max_photon_pulse_width = 100'000;
//     difftime_type max_diff_time = 15'000;
//     abstime_type pixel_time = -1;
//     std::size_t pixels_per_line = 0;
//     std::size_t lines_per_frame = 0;
//     difftime_type bin_width = 50;
//     bin_index_type max_bin_index = 255;
//     bool cumulative = false;
//     bool truncate = false;
//     bool dump_graph = false;
// };

class EventPipeline : public IteratorBase {
public:
    EventPipeline(TimeTagger_PrivateData *data, OScDev_Acquisition *acq, std::shared_ptr<tcspc::context> const &ctx);
    ~EventPipeline();
    int32_t pixelMarker;
    int32_t lineMarker;
    int32_t detectorMarker;
protected:

    bool next_impl(std::vector<Tag> &incoming_tags, timestamp_t begin_time, timestamp_t end_time) override;
    void clear_impl() override final;
    void on_start() override;
    void on_stop() override;

private:
    TimeTagger_PrivateData *tagger;
    tcspc::type_erased_processor<tcspc::type_list<tcspc::swabian_tag_event>> pipeline_;
};

auto make_processor(TimeTagger_PrivateData *data, OScDev_Acquisition *acq, std::shared_ptr<tcspc::context> const &ctx);