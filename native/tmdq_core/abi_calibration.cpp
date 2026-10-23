/**
 * @file abi_calibration.cpp
 * @brief Step files and calibration extraction, through the C ABI.
 *
 * Only translation. Reading a step file is `StepDetector::parseStepConfig`; turning the dialog's inputs into the
 * extractor's request (the sync text decoded like the main run, the default word map resolved like the main
 * run) is `buildCalibrationRequest`; the extraction itself is `CalibrationExtractor`. They are the ones the Qt
 * dialog uses, so a calibration through here and one through the dialog cannot disagree.
 */

#include "tmdq_abi.h"

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "abi_events.h"
#include "abi_internal.h"
#include "calibrationextractor.h"
#include "calibrationprofile.h"
#include "calibrationrequest.h"
#include "stepdetector.h"

namespace {

constexpr std::size_t kCalibrationRequestMinimum = sizeof(tmdq_calibration_request);
constexpr std::size_t kCallbacksMinimum          = sizeof(tmdq_callbacks);
constexpr std::size_t kInfoMinimum               = sizeof(tmdq_calibration_info);
constexpr std::size_t kChannelMinimum            = sizeof(tmdq_calibration_channel);

// A profile's points are handed to the host as they lie in memory, so the two layouts have to be one.
static_assert(sizeof(CalibrationPoint) == sizeof(tmdq_cal_point), "CalibrationPoint and tmdq_cal_point differ in size");
static_assert(offsetof(CalibrationPoint, rawAvg) == offsetof(tmdq_cal_point, raw_avg), "raw_avg is not where rawAvg is");
static_assert(offsetof(CalibrationPoint, trueDb) == offsetof(tmdq_cal_point, true_db), "true_db is not where trueDb is");

} // namespace

/// The steps a step file held.
struct tmdq_steps
{
    std::vector<double> db;
};

/// An extraction: the sink (shared so it outlives the driver thread), the extractor, and the text the
/// results hand out, built once when the extraction has finished.
struct tmdq_calibration
{
    std::shared_ptr<tmdq::CallbackEvents> events;
    CalibrationExtractor                  extractor;
    int                                   expectedSteps = 0;

    std::once_flag textBuilt;
    std::string    summary;
    std::string    error;
    std::string    report;
};

int32_t tmdq_steps_parse(const char* path_utf8, tmdq_steps** out)
{
    tmdq::clearError();
    if (!out)
    {
        tmdq::setError("null output");
        return TMDQ_ERR_BADARG;
    }
    *out = nullptr;
    if (!path_utf8)
    {
        tmdq::setError("null path");
        return TMDQ_ERR_BADARG;
    }

    return tmdq::guarded("tmdq_steps_parse", [&]() -> int32_t {
        std::vector<StepDefinition> steps;
        std::string error;
        if (!StepDetector::parseStepConfig(tmdq::pathFromUtf8(path_utf8), steps, error))
        {
            tmdq::setError(error.empty() ? std::string("the step file could not be read") : error);
            return TMDQ_ERR_PREPARE;
        }
        auto parsed = std::make_unique<tmdq_steps>();
        for (const StepDefinition& step : steps)
        {
            parsed->db.push_back(step.db);
        }
        *out = parsed.release();
        return TMDQ_OK;
    });
}

int32_t tmdq_steps_count(const tmdq_steps* steps) { return steps ? static_cast<int32_t>(steps->db.size()) : -1; }

const double* tmdq_steps_db(const tmdq_steps* steps) { return steps ? steps->db.data() : nullptr; }

void tmdq_steps_free(tmdq_steps* steps) { delete steps; }

int32_t tmdq_step_config_initial_path(const char* app_root_utf8, const char* preferred_utf8, char* buffer,
                                      int32_t capacity)
{
    tmdq::clearError();
    int32_t length = -1;
    const int32_t status = tmdq::guarded("tmdq_step_config_initial_path", [&]() -> int32_t {
        length = tmdq::copyText(StepDetector::initialStepConfigPath(tmdq::textOf(app_root_utf8), tmdq::textOf(preferred_utf8)),
                                buffer, capacity);
        if (length < 0)
        {
            tmdq::setError("a buffer and capacity that do not agree");
        }
        return TMDQ_OK;
    });
    return status == TMDQ_OK ? length : -1;
}

int32_t tmdq_calibration_start(const tmdq_calibration_request* request, const tmdq_callbacks* callbacks,
                               tmdq_calibration** out)
{
    tmdq::clearError();
    if (!out)
    {
        tmdq::setError("null output");
        return TMDQ_ERR_BADARG;
    }
    *out = nullptr;

    return tmdq::guarded("tmdq_calibration_start", [&]() -> int32_t {
        tmdq_calibration_request req{};
        if (!tmdq::readStruct(request, kCalibrationRequestMinimum, req))
        {
            tmdq::setError("null request or a struct_size that is too small");
            return TMDQ_ERR_BADARG;
        }
        if (!req.cal_filename_utf8 || !*req.cal_filename_utf8)
        {
            tmdq::setError("no calibration filename");
            return TMDQ_ERR_BADARG;
        }
        if (req.step_count < 0 || (req.step_count > 0 && !req.step_db))
        {
            tmdq::setError("step_count does not match the step array");
            return TMDQ_ERR_BADARG;
        }

        tmdq_callbacks cb{};
        if (callbacks && !tmdq::readStruct(callbacks, kCallbacksMinimum, cb))
        {
            tmdq::setError("a callbacks struct_size that is too small");
            return TMDQ_ERR_BADARG;
        }

        CalibrationInputs inputs;
        inputs.calFilename      = tmdq::pathFromUtf8(req.cal_filename_utf8);
        inputs.timeChannelId    = req.time_channel_id;
        inputs.pcmChannelId     = req.pcm_channel_id;
        inputs.syncPattern      = tmdq::textOf(req.sync_pattern_utf8);
        inputs.syncMask         = tmdq::textOf(req.sync_mask_utf8);
        inputs.bitsInMinorFrame = req.bits_in_minor_frame;
        inputs.randomized       = req.randomized != 0;
        inputs.inverted         = req.inverted != 0;
        inputs.dataRateMbps     = req.data_rate_mbps;
        inputs.swapBytes        = req.swap_bytes != 0;
        if (req.receiver_params_toml_utf8 && *req.receiver_params_toml_utf8)
        {
            inputs.receiverParamsToml = tmdq::pathFromUtf8(req.receiver_params_toml_utf8);
        }
        inputs.numReceivers     = req.num_receivers;
        inputs.receiverChannels = req.receiver_channels;
        for (int32_t i = 0; i < req.step_count; ++i)
        {
            inputs.steps.push_back(StepDefinition{req.step_db[i]});
        }
        inputs.clipStartSec     = req.clip_start_sec;
        inputs.clipEndSec       = req.clip_end_sec;

        CalibrationExtractor::Request built;
        std::string why;
        if (!buildCalibrationRequest(inputs, tmdq::textOf(req.app_root_utf8), built, why))
        {
            tmdq::setError(why);
            return TMDQ_ERR_JOB;
        }

        auto calibration = std::make_unique<tmdq_calibration>();
        calibration->events        = std::make_shared<tmdq::CallbackEvents>(cb);
        calibration->expectedSteps = static_cast<int>(built.steps.size());
        // Failures the extractor can see before any work starts arrive through the sink, before this returns.
        calibration->extractor.start(built, calibration->events);
        *out = calibration.release();
        return TMDQ_OK;
    });
}

void tmdq_calibration_cancel(tmdq_calibration* calibration)
{
    if (calibration)
    {
        calibration->extractor.cancel();
    }
}

int32_t tmdq_calibration_running(const tmdq_calibration* calibration)
{
    return calibration && calibration->extractor.running() ? 1 : 0;
}

void tmdq_calibration_wait(tmdq_calibration* calibration)
{
    if (calibration)
    {
        calibration->extractor.wait();
    }
}

void tmdq_calibration_free(tmdq_calibration* calibration) { delete calibration; }

namespace {

/// The text a finished calibration hands out is built once, from the extractor's results.
void buildText(tmdq_calibration& c)
{
    std::call_once(c.textBuilt, [&] {
        c.summary = c.extractor.summary();
        c.error   = c.extractor.error();
        c.report  = CalibrationExtractor::summarize(c.extractor.results(), c.expectedSteps);
    });
}

} // namespace

int32_t tmdq_calibration_result(const tmdq_calibration* calibration, tmdq_calibration_info* out)
{
    tmdq::clearError();
    if (!calibration)
    {
        tmdq::setError("null calibration");
        return TMDQ_ERR_BADARG;
    }
    if (calibration->extractor.running())
    {
        tmdq::setError("the calibration is still running");
        return TMDQ_ERR_STATE;
    }
    return tmdq::guarded("tmdq_calibration_result", [&]() -> int32_t {
        auto& c = const_cast<tmdq_calibration&>(*calibration);   // the text is built once, lazily, and then only read
        buildText(c);
        tmdq_calibration_info info{};
        info.channel_count     = static_cast<int32_t>(c.extractor.results().size());
        info.summary_utf8      = c.summary.c_str();
        info.error_utf8        = c.error.c_str();
        info.report_utf8       = c.report.c_str();
        info.recording_seconds = c.extractor.recordingSeconds();
        info.clip_ignored      = c.extractor.clipIgnored() ? 1 : 0;
        if (!tmdq::writeStruct(info, kInfoMinimum, out))
        {
            tmdq::setError("null output or a struct_size that is too small");
            return TMDQ_ERR_BADARG;
        }
        return TMDQ_OK;
    });
}

int32_t tmdq_calibration_channel_result(const tmdq_calibration* calibration, int32_t index,
                                        tmdq_calibration_channel* out)
{
    tmdq::clearError();
    if (!calibration)
    {
        tmdq::setError("null calibration");
        return TMDQ_ERR_BADARG;
    }
    if (calibration->extractor.running())
    {
        tmdq::setError("the calibration is still running");
        return TMDQ_ERR_STATE;
    }
    const std::vector<CalibrationChannelResult>& results = calibration->extractor.results();
    if (static_cast<std::size_t>(index) >= results.size())
    {
        tmdq::setError("channel index out of range");
        return TMDQ_ERR_BADARG;
    }
    const CalibrationChannelResult& r = results[static_cast<std::size_t>(index)];
    tmdq_calibration_channel channel{};
    channel.word_index          = r.word;
    channel.name_utf8           = r.name.c_str();
    channel.points              = reinterpret_cast<const tmdq_cal_point*>(r.profile.points.data());
    channel.plateau_levels      = r.plateauLevels.data();
    channel.unresolved_db       = r.unresolvedDb.data();
    channel.edge_threshold      = r.edgeThreshold;
    channel.point_count         = static_cast<int32_t>(r.profile.points.size());
    channel.plateau_level_count = static_cast<int32_t>(r.plateauLevels.size());
    channel.unresolved_count    = static_cast<int32_t>(r.unresolvedDb.size());
    channel.valid               = r.profile.valid ? 1 : 0;
    channel.had_data            = r.hadData ? 1 : 0;
    channel.outcome             = static_cast<int32_t>(r.outcome);
    channel.detected_plateaus   = r.detectedPlateaus;
    channel.extra_plateaus      = r.extraPlateaus ? 1 : 0;
    channel.threshold_relaxed   = r.thresholdRelaxed ? 1 : 0;
    channel.saturated           = r.saturated ? 1 : 0;
    channel.sweep_levels        = r.sweepLevels;
    if (!tmdq::writeStruct(channel, kChannelMinimum, out))
    {
        tmdq::setError("null output or a struct_size that is too small");
        return TMDQ_ERR_BADARG;
    }
    return TMDQ_OK;
}
