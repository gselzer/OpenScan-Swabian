#include "EventPipeline.h"

#include <bit>

// template <bool Cumulative>
// auto make_histo_proc(
//     TimeTagger_PrivateData *data,
//     OScDev_Acquisition *acq,
//     std::shared_ptr<tcspc::context> const &ctx)
// {
//     using namespace tcspc;
//     auto bsource = recycling_bucket_source<u16>::create();
//     auto writer = write_binary_stream(
//         binary_file_output_stream("foo.txt",
//                                   arg::truncate{false}),
//         recycling_bucket_source<std::byte>::create(),
//         arg::granularity<>{65536});
//     struct reset_event {};
//     uint32_t x, y, width, height;
//     OScDev_Acquisition_GetROI(acq, &x, &y, &width, &height);
//     if constexpr (Cumulative) {
//         return append(
//             reset_event{}, // Reset before flush to get concluding array.
//             scan_histograms<histogram_policy::emit_concluding_events,
//                             reset_event>(
//                 arg::num_elements{std::size_t(width * height)},
//                 arg::num_bins{std::size_t(data->maxBinIndex) + 1},
//                 arg::max_per_bin<u16>{65535}, bsource,
//                 count<histogram_array_event<>>(
//                     ctx->tracker<count_accessor>("frame_counter"),
//                     select<type_list<concluding_histogram_array_event<>>>(
//                         extract_bucket<concluding_histogram_array_event<>>(
//                             view_as_bytes(std::move(writer)))))));
//     } else {
//         return scan_histograms<histogram_policy::clear_every_scan>(
//             arg::num_elements{std::size_t(width * height)},
//             arg::num_bins{std::size_t(data->maxBinIndex) + 1},
//             arg::max_per_bin<u16>{65535}, bsource,
//             select<type_list<histogram_array_event<>>>(
//                 count<histogram_array_event<>>(
//                     ctx->tracker<count_accessor>("frame_counter"),
//                     extract_bucket<histogram_array_event<>>(
//                         view_as_bytes(std::move(writer))))));
//     }
// }

class CallFrameCallbackSink {
    OScDev_Acquisition *acq_;
    uint32_t channel_;
public:
    CallFrameCallbackSink(OScDev_Acquisition *acq, uint32_t channel)
        : acq_(acq), channel_(channel) {}

    void handle(tcspc::histogram_array_event<> const &event) {
        OScDev_Acquisition_CallFrameCallback(
            acq_,
            channel_,
            const_cast<void *>(static_cast<void const *>(event.data_bucket.data())));
    }
    void flush() {}

    [[nodiscard]] auto introspect_node() const -> tcspc::processor_info {
        return tcspc::processor_info(this, "append");
    }

    [[nodiscard]] auto introspect_graph() const -> tcspc::processor_graph {
        return tcspc::processor_graph().push_entry_point(this);
    }
};

template <bool Cumulative>
auto make_histo_proc(
    TimeTagger_PrivateData *,
    OScDev_Acquisition *acq,
    std::shared_ptr<tcspc::context> const &ctx
) {
    using namespace tcspc;
    uint32_t x, y, width, height;
    OScDev_Acquisition_GetROI(acq, &x, &y, &width, &height);
    auto bsource = recycling_bucket_source<u16>::create();
    struct reset_event {};
    if constexpr (Cumulative) {
        return append(
            reset_event{}, // Reset before flush to get concluding array.
            scan_histograms<histogram_policy::emit_concluding_events,
                            reset_event>(
                arg::num_elements{std::size_t(width * height)},
                arg::num_bins{std::size_t(1)},
                arg::max_per_bin<u16>{65535}, bsource,
                count<histogram_array_event<>>(
                    ctx->tracker<count_accessor>("frame_counter"),
                    select<type_list<concluding_histogram_array_event<>>>(
                        CallFrameCallbackSink(acq, 0)))));
    } else {
        return scan_histograms<histogram_policy::clear_every_scan>(
            arg::num_elements{std::size_t(width * height)},
            arg::num_bins{std::size_t(1)},
            arg::max_per_bin<u16>{65535}, bsource,
            select<type_list<histogram_array_event<>>>(
                count<histogram_array_event<>>(
                    ctx->tracker<count_accessor>("frame_counter"),
                        CallFrameCallbackSink(acq, 0))));
    }
}

template <bool Cumulative>
auto make_processor(
    TimeTagger_PrivateData *data,
    OScDev_Acquisition *acq,
    std::shared_ptr<tcspc::context> const &ctx
) {
    using namespace tcspc;

    // clang-format off

    auto [tc_merge, start_stop_merge] =
    merge<type_list<
        time_correlated_detection_event<>,
        pixel_start_event,
        pixel_stop_event,
        time_reached_event<>>>(
            arg::max_buffered<>{1 << 20},
    map_to_datapoints<time_correlated_detection_event<>>(
        difftime_data_mapper(),
    map_to_bins(
        linear_bin_mapper(
            arg::offset{0},
            arg::bin_width{std::int32_t(data->binWidth)},
            arg::max_bin_index{std::uint16_t(data->maxBinIndex)}),
    cluster_bin_increments<pixel_start_event, pixel_stop_event>(
    count<bin_increment_cluster_event<>>(
        ctx->tracker<count_accessor>("pixel_counter"),
    make_histo_proc<Cumulative>(data, acq, ctx))))));

    auto [sync_merge, cfd_merge] =
    merge<type_list<detection_event<>, time_reached_event<>>>(
        arg::max_buffered<>{1 << 20},
    pair_all_between(
        arg::start_channel{data->syncChannel},
        std::array{data->photonChannel},
        arg::time_window<i64>{data->maxDiffTime},
    select<type_list<std::array<detection_event<>, 2>, time_reached_event<>>>(
    time_correlate_at_stop(
    std::move(tc_merge)))));

    auto sync_processor =
    delay(arg::delta{std::int64_t(data->syncDelay)},
    std::move(sync_merge));

    auto photon_processor =
    pair_one_between(
        arg::start_channel{data->photonChannel},
        std::array{data->photonChannel},
        arg::time_window{std::int64_t(data->maxPhotonPulseWidth)},
    select<type_list<std::array<detection_event<>, 2>, time_reached_event<>>>(
    time_correlate_at_midpoint(
    remove_time_correlation(
    recover_order<type_list<detection_event<>, time_reached_event<>>>(
        arg::time_window{std::int64_t(data->maxPhotonPulseWidth)},
    std::move(cfd_merge))))));

    double pixelRate = OScDev_Acquisition_GetPixelRate(acq);
    auto pixel_marker_processor =
    match<detection_event<>, pixel_start_event>(always_matcher(),
    select<type_list<pixel_start_event, time_reached_event<>>>(
    generate<pixel_start_event, pixel_stop_event>(
        one_shot_timing_generator(arg::delay{std::int64_t(1e12 / pixelRate)}),
    check_alternating<pixel_start_event, pixel_stop_event>(
    stop_with_error<type_list<warning_event>>(
        "pixel time is such that pixel stop occurs after next pixel start",
    std::move(start_stop_merge))))));

    return
    decode_swabian_tags(
    count<detection_event<>>(ctx->tracker<count_accessor>("record_counter"),
    stop_with_error<type_list<
        warning_event,
        begin_lost_interval_event<>,
        end_lost_interval_event<>,
        lost_counts_event<>>>("error in input data",
    check_monotonic(
    stop<type_list<warning_event>>("processing stopped",
    regulate_time_reached(
        arg::interval_threshold<abstime_type>{1 << 30}, // About 1 ms
        arg::count_threshold<>{1 << 18}, // 1/4 of merge buffer size
    route<type_list<detection_event<>>, type_list<time_reached_event<>>>(
        channel_router(std::array{
            std::pair{data->syncChannel, 0},
            std::pair{data->photonChannel, 1},
            std::pair{-1 * data->photonChannel, 1},
            std::pair{data->pixelMarkerChannel, 2},
        }),
        std::move(sync_processor),
        std::move(photon_processor),
        std::move(pixel_marker_processor))))))));

    // clang-format on
};

EventPipeline::EventPipeline(TimeTagger_PrivateData *data, OScDev_Acquisition *acq, std::shared_ptr<tcspc::context> const &ctx) : IteratorBase(data->tagger),
    pipeline_(make_processor<false>(data, acq, ctx))
{
    registerChannel(data->syncChannel);
    registerChannel(data->photonChannel);
    registerChannel(-1 * data->photonChannel);
    registerChannel(data->pixelMarkerChannel);
    finishInitialization();
}

EventPipeline::~EventPipeline() {
    stop();
}

bool EventPipeline::next_impl(std::vector<Tag> &incoming_tags, timestamp_t begin_time, timestamp_t end_time) {
    // TODO: One idea is that, to minimize the chance of tag buildup (and then losing data),
    // all this thread should do is to push the tags onto a libtcspc buffer (which we will have to add to the pipeline),
    // and then have another thread responsible for pumping tags out of the buffer. That would require a little more
    // coordination. Mark is pretty sure this will be needed.
    //
    // TODO: Create and handle TimeReachedEvents for the end_time timestamp (can't hurt to do begin_time as well)
    for (auto const &tag : incoming_tags) {
        pipeline_.handle(std::bit_cast<tcspc::swabian_tag_event>(tag));
    }
    return false;
}

void EventPipeline::clear_impl() {
}

void EventPipeline::on_start() {
}

void EventPipeline::on_stop() {
    pipeline_.flush();
}
