#include "EventPipeline.h"
#include "UniqueFileName.h"

#include <bit>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>


// Calls OpenScanLib's frame callback with a properly-sized width*height
// buffer (one u16 sample per pixel -- see OpenScanDeviceLib.h's
// GetBytesPerSample/GetNumberOfChannels/CallFrameCallback docs: "values
// other than 2 (16-bit) are not currently supported" and "the raw pixel
// data for the channel", i.e. no room for a per-pixel histogram cube).
// This is therefore only ever wired to the 1-bin ("intensity") branch of
// the broadcast in make_processor -- see the comment there.
class CallFrameCallbackSink {
    OScDev_Acquisition *acq_;
    uint32_t channel_;
    uint32_t num_frames_;
    uint32_t frames_delivered_ = 0;
public:
    CallFrameCallbackSink(OScDev_Acquisition *acq, uint32_t channel,
                           uint32_t num_frames)
        : acq_(acq), channel_(channel), num_frames_(num_frames) {}

    void handle(tcspc::histogram_array_event<> const &event) {
        handle_bucket(event.data_bucket);
    }
    // Cumulative mode's scan_histograms<emit_concluding_events> emits a
    // concluding_histogram_array_event instead of histogram_array_event
    // (a distinct, unrelated type, despite carrying the same data_bucket
    // shape) -- this sink needs to be a valid handler for both, since
    // make_processor<true> selects only the former as this sink's input.
    void handle(tcspc::concluding_histogram_array_event<> const &event) {
        handle_bucket(event.data_bucket);
    }
    void flush() {}

    [[nodiscard]] auto introspect_node() const -> tcspc::processor_info {
        return tcspc::processor_info(this, "CallFrameCallbackSink");
    }

    [[nodiscard]] auto introspect_graph() const -> tcspc::processor_graph {
        return tcspc::processor_graph().push_entry_point(this);
    }

private:
    template <typename Bucket> void handle_bucket(Bucket const &data_bucket) {
        OScDev_Acquisition_CallFrameCallback(
            acq_,
            channel_,
            const_cast<void *>(static_cast<void const *>(data_bucket.data())));
        // Same idea as BH's LineClockPixellator calling downstream->
        // HandleFinish() once currentLine / linesPerFrame == maxFrames --
        // stop once the requested number of frames has been delivered, via
        // the same clean-completion protocol as any other stop condition
        // in this pipeline (see EventPipeline::next_impl()'s catch for
        // tcspc::end_of_processing). Matches BH: it's IntensityImageSink,
        // not HistogramSink, that calls stopFunc() -- the live/intensity
        // branch owns completion, not the full-histogram branch.
        if (++frames_delivered_ == num_frames_)
            throw tcspc::end_of_processing(
                "acquisition complete: reached requested frame count");
    }
};

// DEBUG: terminal sink for the full per-pixel histogram (histogramBins
// bins/pixel) -- writes it to a known location for offline inspection
// instead of sending it to OpenScanLib (which has no way to receive a
// per-pixel histogram cube through CallFrameCallback -- see
// CallFrameCallbackSink's comment above). No frame-count stop logic here;
// the live/intensity branch (CallFrameCallbackSink) owns that. Remove, or
// replace with real FLIM file output, once no longer needed.
class HistogramDumpSink {
public:
    void handle(tcspc::histogram_array_event<> const &event) {
        dump_bucket(event.data_bucket);
    }
    // Cumulative mode's scan_histograms<emit_concluding_events> emits a
    // concluding_histogram_array_event instead of histogram_array_event
    // (a distinct, unrelated type, despite carrying the same data_bucket
    // shape) -- this sink needs to be a valid handler for both, since
    // make_full_histo_proc<true> selects only the former as this sink's
    // input.
    void handle(tcspc::concluding_histogram_array_event<> const &event) {
        dump_bucket(event.data_bucket);
    }
    void flush() {}

    [[nodiscard]] auto introspect_node() const -> tcspc::processor_info {
        return tcspc::processor_info(this, "HistogramDumpSink");
    }

    [[nodiscard]] auto introspect_graph() const -> tcspc::processor_graph {
        return tcspc::processor_graph().push_entry_point(this);
    }

private:
    template <typename Bucket> void dump_bucket(Bucket const &data_bucket) {
        std::ofstream debug_file(
            "C:\\Users\\gjselzer\\code\\openscan-lsm\\OpenScan-Swabian\\histogram_debug.bin",
            std::ios::binary | std::ios::trunc);
        debug_file.write(
            reinterpret_cast<char const *>(data_bucket.data()),
            static_cast<std::streamsize>(data_bucket.size() *
                                          sizeof(tcspc::u16)));
    }
};

// Writes every raw tag to disk, in the vendor SDK's raw dump format
// (back-to-back 16-byte records, decodable with tools/dump_tags.py) --
// same format as the real SDK's Dump measurement, and what "Save Raw
// Data" used to be wired to (a second, independent Dump/IteratorBase
// instance pulling directly from the tagger). That approach doesn't work
// in simulate mode: each IteratorBase there runs its own PumpLoop with
// its own independently-seeded RNG, so two separate measurements
// "watching the same channels" would each generate a DIFFERENT random
// realization of the gated-photon noise -- the dump would never actually
// match what the live pipeline processed. Tapping the live tag stream
// here instead (via the broadcast in make_processor, gated on
// saveRawData) guarantees the dump is byte-for-byte what was actually
// processed, and matches real hardware too, where there's only one
// physical event stream feeding every consumer either way.
class RawTagDumpSink {
    std::ofstream file_;
public:
    explicit RawTagDumpSink(std::string const &filename)
        : file_(filename, std::ios::binary | std::ios::trunc) {
        if (!file_)
            throw std::runtime_error(
                "RawTagDumpSink: could not open file '" + filename + "'");
    }

    void handle(tcspc::swabian_tag_event const &event) {
        file_.write(reinterpret_cast<char const *>(event.bytes.data()),
                     static_cast<std::streamsize>(event.bytes.size()));
    }
    void flush() { file_.flush(); }

    [[nodiscard]] auto introspect_node() const -> tcspc::processor_info {
        return tcspc::processor_info(this, "RawTagDumpSink");
    }

    [[nodiscard]] auto introspect_graph() const -> tcspc::processor_graph {
        return tcspc::processor_graph().push_entry_point(this);
    }
};

// Full per-pixel histogram (histogramBins bins/pixel) -- debug-dumped to
// disk, not sent to OpenScanLib. See HistogramDumpSink's comment.
template <bool Cumulative>
auto make_full_histo_proc(
    TimeTagger_PrivateData *data,
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
                arg::num_bins{std::size_t(data->histogramBins)},
                arg::max_per_bin<u16>{65535}, bsource,
                count<histogram_array_event<>>(
                    ctx->tracker<count_accessor>("full_frame_counter"),
                    select<type_list<concluding_histogram_array_event<>>>(
                        HistogramDumpSink()))));
    } else {
        return scan_histograms<histogram_policy::clear_every_scan>(
            arg::num_elements{std::size_t(width * height)},
            arg::num_bins{std::size_t(data->histogramBins)},
            arg::max_per_bin<u16>{65535}, bsource,
            select<type_list<histogram_array_event<>>>(
                count<histogram_array_event<>>(
                    ctx->tracker<count_accessor>("full_frame_counter"),
                        HistogramDumpSink())));
    }
}

// Single-bin ("intensity") histogram -- one count per pixel, timing
// ignored, sent live to OpenScanLib via CallFrameCallbackSink. This is
// the branch OpenScanLib actually displays, and the one responsible for
// signaling acquisition completion (see CallFrameCallbackSink's comment).
template <bool Cumulative>
auto make_live_histo_proc(
    TimeTagger_PrivateData * /*data*/, // unused: the intensity branch's
                                        // binning is fixed (1 bin, clamped),
                                        // not derived from histogramBins
    OScDev_Acquisition *acq,
    std::shared_ptr<tcspc::context> const &ctx
) {
    using namespace tcspc;
    uint32_t x, y, width, height;
    OScDev_Acquisition_GetROI(acq, &x, &y, &width, &height);
    auto bsource = recycling_bucket_source<u16>::create();
    uint32_t const num_frames = OScDev_Acquisition_GetNumberOfFrames(acq);
    // scan_histograms emits a histogram_array_event as soon as each frame's
    // scan finishes. clear_every_scan clears the arrays, meaning fresh,
    // non-cumulative frames; the default policy leaves prior counts in place
    // for cumulative frames.
    constexpr histogram_policy policy = Cumulative
                                            ? histogram_policy::default_policy
                                            : histogram_policy::clear_every_scan;
    return scan_histograms<policy>(
        arg::num_elements{std::size_t(width * height)},
        arg::num_bins{std::size_t(1)},
        arg::max_per_bin<u16>{65535}, bsource,
        select<type_list<histogram_array_event<>>>(
            count<histogram_array_event<>>(
                ctx->tracker<count_accessor>("frame_counter"),
                    CallFrameCallbackSink(acq, 0, num_frames))));
}

template <bool Cumulative>
auto make_processor(
    TimeTagger_PrivateData *data,
    OScDev_Acquisition *acq,
    std::shared_ptr<tcspc::context> const &ctx
) {
    using namespace tcspc;

    // clang-format off

    uint32_t x, y, width, height;
    OScDev_Acquisition_GetROI(acq, &x, &y, &width, &height);

    // pair_all_between guarantees every correlated photon's difftime is
    // less than maxDiffTime, so the histogram's covered range
    // (bin_width * num_bins) must be at least maxDiffTime for none of them
    // to be silently dropped by the bin mapper. Ceiling division instead
    // always rounds bin_width UP, so bin_width * num_bins >= maxDiffTime
    // always holds; the only cost is the covered range overshooting
    // maxDiffTime by at most num_bins - 1 ps (one bin's worth of rounding
    // error spread across the whole histogram), which is a far cheaper
    // trade than losing real data.
    std::int32_t const num_bins = data->histogramBins;
    difftime_type const bin_width =
        (data->maxDiffTime + num_bins - 1) / num_bins;

    using tc_event_list = type_list<
        time_correlated_detection_event<>,
        pixel_start_event,
        pixel_stop_event,
        time_reached_event<>>;

    // Single-bin "intensity" equivalent (max_bin_index=0, clamp=true forces
    // every photon into bin 0 regardless of its difftime) -- this is the
    // one actually sent live to OpenScanLib via CallFrameCallback, since
    // that contract only supports one u16 sample per pixel (see
    // CallFrameCallbackSink's comment). Always built: this is the live
    // image path.
    auto live_pixel_chain =
    map_to_datapoints<time_correlated_detection_event<>>(
        difftime_data_mapper(),
    map_to_bins(
        linear_bin_mapper(
            arg::offset<difftime_type>{0},
            arg::bin_width<difftime_type>{1},
            arg::max_bin_index<bin_index_type>{0},
            arg::clamp{true}),
    cluster_bin_increments<pixel_start_event, pixel_stop_event>(
    count<bin_increment_cluster_event<>>(
        ctx->tracker<count_accessor>("live_pixel_counter"),
    make_live_histo_proc<Cumulative>(data, acq, ctx)))));

    // The full per-pixel histogram (histogramBins bins/pixel, debug-dumped
    // to disk by HistogramDumpSink) is only useful when the "Save
    // Histograms" setting is on. Broadcasting every time-correlated event
    // to it as well as to live_pixel_chain roughly doubles consumer-side
    // per-event work (map_to_datapoints -> map_to_bins ->
    // cluster_bin_increments -> scan_histograms, all over again, plus the
    // dump itself), for no benefit when nobody's consuming it. So it's
    // built -- and the broadcast exists at all -- only inside this branch;
    // when the setting is off, tc_downstream is just live_pixel_chain,
    // type-erased to the same interface so both arms of the branch have a
    // common type to hand to merge() below.
    type_erased_processor<tc_event_list> tc_downstream =
        [&]() -> type_erased_processor<tc_event_list> {
        if (!data->saveHistograms)
            return type_erased_processor<tc_event_list>(
                std::move(live_pixel_chain));

        // Full per-pixel TCSPC histogram (derived bin_width, real
        // histogramBins) -- debug-dumped to disk, not sent to OpenScanLib.
        auto full_pixel_chain =
        map_to_datapoints<time_correlated_detection_event<>>(
            difftime_data_mapper(),
        map_to_bins(
            linear_bin_mapper(
                arg::offset<difftime_type>{0},
                arg::bin_width{bin_width},
                // linear_bin_mapper's own parameter is the index of the
                // last bin (num_bins - 1), not a bin count -- num_bins is
                // guaranteed >= 16 by HistogramBinsSetting's
                // discrete-values list (never includes 0), so this can't
                // underflow.
                arg::max_bin_index<bin_index_type>{bin_index_type(num_bins - 1)}),
        cluster_bin_increments<pixel_start_event, pixel_stop_event>(
        count<bin_increment_cluster_event<>>(
            ctx->tracker<count_accessor>("pixel_counter"),
        make_full_histo_proc<Cumulative>(data, acq, ctx)))));

        return type_erased_processor<tc_event_list>(
            broadcast<tc_event_list>(
                std::move(full_pixel_chain),
                std::move(live_pixel_chain)));
    }();

    auto [tc_merge, start_stop_merge] =
    merge<tc_event_list>(
        arg::max_buffered<>{1 << 20},
        std::move(tc_downstream));

    auto [sync_merge, cfd_merge] =
    merge<type_list<detection_event<>, time_reached_event<>>>(
        arg::max_buffered<>{1 << 20},
    pair_all_between(
        arg::start_channel{data->syncChannel},
        std::array{data->photonChannel},
        arg::time_window<abstime_type>{data->maxDiffTime},
    select<type_list<std::array<detection_event<>, 2>, time_reached_event<>>>(
    time_correlate_at_stop(
    std::move(tc_merge)))));

    auto sync_processor =
    delay(arg::delta<abstime_type>{data->syncDelay},
    std::move(sync_merge));

    auto photon_processor =
    pair_one_between(
        arg::start_channel{data->photonChannel},
        // Stop channel is the FALLING edge of the same pulse (negative
        // channel number -- see decode_swabian_tags/the raw dump's
        // "channel=-3" convention), not the rising edge again -- pairs a
        // photon pulse's rise with its own fall, per "Max Photon Pulse
        // Width"'s meaning.
        std::array{-data->photonChannel},
        arg::time_window<abstime_type>{data->maxPhotonPulseWidth},
    select<type_list<std::array<detection_event<>, 2>, time_reached_event<>>>(
    // UseStartChannel=true: stamp the emitted pulse event's channel from
    // the start (rising, +photonChannel) side of the pair, not the stop
    // (falling, -photonChannel) side -- downstream (the sync/photon
    // pair_all_between below) matches on +photonChannel, so a pulse
    // event carrying the falling edge's negative channel would never
    // correlate with anything.
    time_correlate_at_midpoint<default_numeric_traits, true>(
    remove_time_correlation(
    recover_order<type_list<detection_event<>, time_reached_event<>>>(
        arg::time_window<abstime_type>{data->maxPhotonPulseWidth},
    std::move(cfd_merge))))));


    double pixelRate = OScDev_Acquisition_GetPixelRate(acq);
    auto pixel_marker_processor =
    // Convert line clock detection events into (width + 1) pixel tick events
    generate<detection_event<>, pixel_tick_event>(
        linear_timing_generator(
            arg::delay<abstime_type>{data->lineDelay},
            arg::interval<abstime_type>{abstime_type(1e12 / pixelRate)},
            arg::count{std::size_t(width) + 1}
        ),
    // Convert (width) pixel tick events into (width) "pixel start + pixel stop" intervals
    convert_sequences_to_start_stop<pixel_tick_event, pixel_start_event, pixel_stop_event>(
        arg::count{std::size_t(width)},
    // Filter out the line clock events
    select<type_list<pixel_start_event, pixel_stop_event, time_reached_event<>>>(
    // Enforce pixel start/pixel stop alternation
    check_alternating<pixel_start_event, pixel_stop_event>(
    stop_with_error<type_list<warning_event>>(
        "pixel time is such that pixel stop occurs after next pixel start",
    std::move(start_stop_merge))))));

    using raw_event_list = type_list<swabian_tag_event>;

    auto rest_of_chain =
    decode_swabian_tags(
    count<detection_event<>>(ctx->tracker<count_accessor>("record_counter"),
    // TODO: On real hardware, a fixed-size circular FIFO means that when
    // software falls behind, old tags get overwritten rather than
    // unboundedly retained -- the device signals this by emitting
    // OverflowBegin/OverflowEnd/MissedEvents tags (Tag::Type in
    // TimeTagger.h), which decode_swabian_tags turns into the
    // begin_lost_interval_event<>/end_lost_interval_event<>/
    // lost_counts_event<> handled right below. IteratorBase::PumpLoop
    // (fake_timetagger/include/TimeTagger.h) currently has no notion of
    // "falling behind" at all -- it unconditionally generates and retains
    // every tag for however much simulated time has elapsed, however long
    // that takes. To model real behavior (and let this exact
    // stop_with_error path actually be exercised, instead of an
    // ever-growing backlog), PumpLoop should detect when it can't keep up
    // (e.g. via something like the reverted kMaxSimulatedStepPs idea) and
    // emit synthetic overflow tags for the excess span instead of
    // silently retaining or silently dropping it. Also worth reconsidering
    // once that exists: whether stop_with_error (a hard stop) is still the
    // right response to a lost interval, versus something more like a
    // recoverable warning.
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
            std::pair{data->lineClockChannel, 2},
        }),
        std::move(sync_processor),
        std::move(photon_processor),
        std::move(pixel_marker_processor))))))));

    // Raw-tag dump, gated on "Save Raw Data" -- see RawTagDumpSink's
    // comment for why this taps the live stream via a broadcast here
    // instead of running as a second, independent Dump/IteratorBase
    // measurement. Filename follows OpenScan-BH_SPC's own scheme
    // (File Name Prefix setting + UniqueFileName's "_NNNN" index) --
    // see UniqueFileName.h for why, and a note on alternatives.
    type_erased_processor<raw_event_list> raw_tag_downstream =
        [&]() -> type_erased_processor<raw_event_list> {
        if (!data->saveRawData)
            return type_erased_processor<raw_event_list>(
                std::move(rest_of_chain));

        std::optional<std::string> const unique_name =
            UniqueFileName(data->fileNamePrefix, {".raw"});
        if (!unique_name)
            throw std::runtime_error(
                "Could not find a unique file name for raw data (prefix '" +
                data->fileNamePrefix + "')");

        return type_erased_processor<raw_event_list>(
            broadcast<raw_event_list>(
                RawTagDumpSink(*unique_name + ".raw"),
                std::move(rest_of_chain)));
    }();

    return

    batch<swabian_tag_event>(
        recycling_bucket_source<swabian_tag_event>::create(),
        arg::batch_size<std::size_t>{1 << 15},
    real_time_buffer<bucket<swabian_tag_event>>(
        arg::threshold<std::size_t>{2},
        std::chrono::milliseconds{500},
        ctx->tracker<buffer_accessor>("tag_buffer"),
    unbatch<bucket<swabian_tag_event>>(
        std::move(raw_tag_downstream))));
    // clang-format on
};

namespace {
tcspc::type_erased_processor<tcspc::type_list<tcspc::swabian_tag_event>>
make_pipeline(TimeTagger_PrivateData *data, OScDev_Acquisition *acq, std::shared_ptr<tcspc::context> const &ctx) {
    using pipeline_type = tcspc::type_erased_processor<tcspc::type_list<tcspc::swabian_tag_event>>;
    if (data->cumulative)
        return pipeline_type(make_processor<true>(data, acq, ctx));
    return pipeline_type(make_processor<false>(data, acq, ctx));
}
} // namespace

EventPipeline::EventPipeline(OScDev_Device *device, OScDev_Acquisition *acq, std::shared_ptr<tcspc::context> const &ctx) : IteratorBase(GetData(device)->tagger),
    device_(device),
    pipeline_(make_pipeline(GetData(device), acq, ctx)),
    accessor_(ctx->access<tcspc::buffer_accessor>("tag_buffer"))
{
    auto* data = GetData(device);
    for (auto const &channel : {data->syncChannel, data->photonChannel, data->lineClockChannel}) {
        registerChannel(channel);
        registerChannel(-1 * channel);
    }
    consumer_thread_ = std::thread([this]() { PumpConsumerLoop(); });
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
    OScDev_Log_Info(device_, ("EventPipeline::next_impl: " + std::to_string(incoming_tags.size()) + " tags, begin_time= " + std::to_string(begin_time) + ", end_time= " + std::to_string(end_time)).c_str());
    try {
        for (auto const &tag : incoming_tags) {
            pipeline_.handle(std::bit_cast<tcspc::swabian_tag_event>(tag));
        }
    } catch (tcspc::end_of_processing const &e) {
        // Documented libtcspc protocol (see errors.hpp): a processor
        // signals a clean, non-error completion by flushing its own
        // downstream and throwing this; we are "the data source" that's
        // required to catch it, and must not send it any more events
        // afterward. finish_running() is safe to call from here (unlike
        // stop()) since next_impl() already runs under IteratorBase's own
        // lock.
        OScDev_Log_Info(device_, ("EventPipeline: acquisition complete: " + std::string(e.what())).c_str());
        finish_running();
    } catch (std::exception const &e) {
        // Any other exception (e.g. stop_with_error's std::runtime_error)
        // is a genuine error in the data -- can't continue either way,
        // but log it distinctly from a normal completion.
        OScDev_Log_Error(device_, ("EventPipeline: pipeline error: " + std::string(e.what())).c_str());
        finish_running();
    }
    OScDev_Log_Info(device_, "EventPipeline::next_impl: handled all those tags");
    return false;
}

void EventPipeline::clear_impl() {
}

void EventPipeline::on_start() {
}

void EventPipeline::on_stop() {
    // Preempt remaining tag batches with a halt.
    accessor_.halt();
    // Finish the current batch and then exit.
    consumer_thread_.join();
}

void EventPipeline::PumpConsumerLoop() {
    try {
        accessor_.pump();
    } catch (tcspc::end_of_processing const &e) {
        // Documented libtcspc protocol (see errors.hpp): a processor
        // signals a clean, non-error completion by flushing its own
        // downstream and throwing this; we are "the data source" that's
        // required to catch it, and must not send it any more events
        // afterward. finish_running() is safe to call from here (unlike
        // stop()) since next_impl() already runs under IteratorBase's own
        // lock.
        OScDev_Log_Info(device_, ("EventPipeline: acquisition complete: " + std::string(e.what())).c_str());
        finish_running();
    } catch (tcspc::source_halted const &) {
        OScDev_Log_Info(
            device_,
            "EventPipeline: consumer halted, remaining tags discarded"
        );
    } catch (std::exception const &e) {
        // Any other exception (e.g. stop_with_error's std::runtime_error)
        // is a genuine error in the data -- can't continue either way,
        // but log it distinctly from a normal completion.
        OScDev_Log_Error(device_, ("EventPipeline: pipeline error: " + std::string(e.what())).c_str());
        finish_running();
    }
}
