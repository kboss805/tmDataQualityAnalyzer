/**
 * @file abi_run.cpp
 * @brief Runs of N streams over one recording, through the C ABI.
 *
 * This file only translates. The pipeline (one reader, a queue and a worker thread per stream, the order
 * they start and stop in) is `PipelineRunner`; turning a stream's settings into a job, and every message a
 * rejected setting produces, is `buildStreamJob`. They are the same two the Qt application uses, so a run
 * through here and a run through `MainViewModel` cannot disagree about what a setting means.
 */

#include "tmdq_abi.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "abi_events.h"
#include "abi_internal.h"
#include "calibrationprofile.h"
#include "pipelinerunner.h"
#include "processedstreamdata.h"
#include "processingparams.h"
#include "streamjobbuilder.h"

namespace {

constexpr std::size_t kStreamConfigMinimum = sizeof(tmdq_stream_config);
constexpr std::size_t kRunRequestMinimum   = sizeof(tmdq_run_request);
constexpr std::size_t kCallbacksMinimum    = sizeof(tmdq_callbacks);
constexpr std::size_t kStreamInfoMinimum   = sizeof(tmdq_stream_info);
constexpr std::size_t kSeriesMinimum       = sizeof(tmdq_channel_series);

} // namespace

namespace {

using tmdq::CallbackEvents;

/// A reason this setup is not usable, as the application words it.
constexpr const char* kNoStreams = "No streams selected for processing. Configure streams first.";

bool toStreamMode(int32_t value, StreamMode& mode)
{
    switch (value)
    {
        case TMDQ_MODE_RECEIVER_CHANNEL_INFO: mode = StreamMode::ReceiverChannelInfo; return true;
        case TMDQ_MODE_FRAME_SYNC_LOCK:       mode = StreamMode::FrameSyncLockStats; return true;
        default:                              return false;
    }
}

/// Reads one stream's settings off the wire. @return false with a reason in @p error.
bool readStreamConfig(const tmdq_stream_config* wire, StreamJobConfig& out, std::string& error)
{
    tmdq_stream_config c{};
    if (!tmdq::readStruct(wire, kStreamConfigMinimum, c))
    {
        error = "a stream config is null or its struct_size is too small";
        return false;
    }
    if (!toStreamMode(c.mode, out.mode))
    {
        error = "unknown stream mode " + std::to_string(c.mode);
        return false;
    }
    if (c.calibration_count < 0 || (c.calibration_count > 0 && !c.calibration))
    {
        error = "calibration_count does not match the calibration array";
        return false;
    }

    out.pcmChannelId      = c.pcm_channel_id;
    out.label             = tmdq::textOf(c.label_utf8);
    out.syncPattern       = tmdq::textOf(c.sync_pattern_utf8);
    out.syncMask          = tmdq::textOf(c.sync_mask_utf8);
    out.bitsInMinorFrame  = c.bits_in_minor_frame;
    out.randomized        = c.randomized != 0;
    out.inverted          = c.inverted != 0;
    out.dataRateMbps      = c.data_rate_mbps;
    out.samplePeriodIndex = c.sample_period_index;
    out.polarityIndex     = c.polarity_index;
    out.slopeIndex        = c.slope_index;
    out.scaleDdBPerV      = c.scale_d_db_per_v;
    out.numReceivers      = c.num_receivers;
    out.receiverChannels  = c.receiver_channels;
    if (c.receiver_params_toml_utf8 && *c.receiver_params_toml_utf8)
    {
        out.receiverParamsToml = tmdq::pathFromUtf8(c.receiver_params_toml_utf8);
    }

    for (int32_t i = 0; i < c.calibration_count; ++i)
    {
        const tmdq_cal_profile& wireProfile = c.calibration[i];
        if (wireProfile.point_count < 0 || (wireProfile.point_count > 0 && !wireProfile.points))
        {
            error = "a calibration profile's point_count does not match its points";
            return false;
        }
        CalibrationProfile profile;
        profile.valid = wireProfile.valid != 0;
        profile.points.reserve(static_cast<std::size_t>(wireProfile.point_count));
        for (int32_t p = 0; p < wireProfile.point_count; ++p)
        {
            profile.points.push_back({wireProfile.points[p].raw_avg, wireProfile.points[p].true_db});
        }
        out.calibrationByWord.emplace(wireProfile.word_index, std::move(profile));   // the first of a word wins
    }
    return true;
}

} // namespace

/// A run: the runner and what it reports to. The runner joins its threads in its destructor, so freeing
/// the handle cancels a run in flight and waits for it.
struct tmdq_run
{
    std::shared_ptr<CallbackEvents> events;
    PipelineRunner                  runner;
};

int32_t tmdq_run_start(const tmdq_run_request* request, const tmdq_callbacks* callbacks, tmdq_run** out)
{
    tmdq::clearError();
    if (!out)
    {
        tmdq::setError("null output");
        return TMDQ_ERR_BADARG;
    }
    *out = nullptr;

    return tmdq::guarded("tmdq_run_start", [&]() -> int32_t {
        tmdq_run_request req{};
        if (!tmdq::readStruct(request, kRunRequestMinimum, req))
        {
            tmdq::setError("null request or a struct_size that is too small");
            return TMDQ_ERR_BADARG;
        }
        if (!req.filename_utf8 || !*req.filename_utf8)
        {
            tmdq::setError("no filename");
            return TMDQ_ERR_BADARG;
        }
        if (req.time_channel_id < 0)
        {
            tmdq::setError("Invalid time channel.");
            return TMDQ_ERR_BADARG;
        }
        if (req.stream_count <= 0 || !req.streams)
        {
            tmdq::setError(kNoStreams);
            return TMDQ_ERR_BADARG;
        }

        tmdq_callbacks cb{};
        if (callbacks && !tmdq::readStruct(callbacks, kCallbacksMinimum, cb))
        {
            tmdq::setError("a callbacks struct_size that is too small");
            return TMDQ_ERR_BADARG;
        }
        auto events = std::make_shared<CallbackEvents>(cb);

        // What is true of the whole file; each stream's own settings come with the stream.
        ProcessingParams base;
        base.filename      = tmdq::pathFromUtf8(req.filename_utf8);
        base.sourceId      = req.source_id;
        base.timeChannelId = req.time_channel_id;
        base.startSeconds  = 0;
        base.stopSeconds   = std::numeric_limits<uint64_t>::max();

        StreamJobContext context;
        context.appRoot   = tmdq::textOf(req.app_root_utf8);
        context.swapBytes = req.swap_bytes != 0;

        std::vector<StreamJob> jobs;
        for (int32_t i = 0; i < req.stream_count; ++i)
        {
            std::string why;
            StreamJobConfig config;
            if (!readStreamConfig(req.streams[i], config, why))
            {
                tmdq::setError("stream " + std::to_string(i) + ": " + why);
                return TMDQ_ERR_BADARG;
            }

            StreamJob job;
            StreamJobReport report;
            if (!buildStreamJob(config, base, context, job, report))
            {
                tmdq::setError(report.error);
                return TMDQ_ERR_JOB;
            }
            for (const std::string& line : report.log)
            {
                events->logMessage(line);
            }
            jobs.push_back(std::move(job));
        }

        auto run = std::make_unique<tmdq_run>();
        run->events = events;
        std::string startError;
        if (!run->runner.start(std::move(jobs), events, startError))
        {
            tmdq::setError(startError.empty() ? std::string("PipelineRunner::start failed") : startError);
            return TMDQ_ERR_PREPARE;
        }
        *out = run.release();
        return TMDQ_OK;
    });
}

void tmdq_run_cancel(tmdq_run* run)
{
    if (run)
    {
        run->runner.cancel();
    }
}

int32_t tmdq_run_running(const tmdq_run* run) { return run && run->runner.running() ? 1 : 0; }

void tmdq_run_wait(tmdq_run* run)
{
    if (run)
    {
        run->runner.wait();
    }
}

void tmdq_run_free(tmdq_run* run) { delete run; }

int32_t tmdq_stream_result_info(const tmdq_stream_result* result, tmdq_stream_info* out)
{
    tmdq::clearError();
    if (!result)
    {
        tmdq::setError("null result");
        return TMDQ_ERR_BADARG;
    }
    const ProcessedStreamData& d = result->data;
    tmdq_stream_info info{};
    info.pcm_channel_id = d.pcmChannelId;
    info.label_utf8     = d.streamLabel.c_str();
    info.times_sec      = d.timesSec.data();
    info.lock_percent   = d.lockPercent.data();
    info.missed_frames  = d.accumulatedMissedFrames.data();
    info.job_index      = d.jobIndex;
    info.source_id      = d.sourceId;
    info.mode           = d.mode == StreamMode::FrameSyncLockStats ? TMDQ_MODE_FRAME_SYNC_LOCK
                                                                    : TMDQ_MODE_RECEIVER_CHANNEL_INFO;
    info.sample_count   = static_cast<int32_t>(d.timesSec.size());
    info.channel_count  = static_cast<int32_t>(d.channels.size());
    if (!tmdq::writeStruct(info, kStreamInfoMinimum, out))
    {
        tmdq::setError("null output or a struct_size that is too small");
        return TMDQ_ERR_BADARG;
    }
    return TMDQ_OK;
}

int32_t tmdq_stream_result_channel(const tmdq_stream_result* result, int32_t index, tmdq_channel_series* out)
{
    tmdq::clearError();
    if (!result)
    {
        tmdq::setError("null result");
        return TMDQ_ERR_BADARG;
    }
    // A negative index wraps to a huge size_t, so the one comparison refuses both ends.
    if (static_cast<std::size_t>(index) >= result->data.channels.size())
    {
        tmdq::setError("channel index out of range");
        return TMDQ_ERR_BADARG;
    }
    const ProcessedChannelSeries& s = result->data.channels[static_cast<std::size_t>(index)];
    tmdq_channel_series series{};
    series.word_index = s.word;
    series.name_utf8  = s.name.c_str();
    series.values     = s.values.data();
    if (!tmdq::writeStruct(series, kSeriesMinimum, out))
    {
        tmdq::setError("null output or a struct_size that is too small");
        return TMDQ_ERR_BADARG;
    }
    return TMDQ_OK;
}

void tmdq_stream_result_free(tmdq_stream_result* result) { delete result; }
