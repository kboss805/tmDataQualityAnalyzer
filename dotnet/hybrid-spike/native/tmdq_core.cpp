/**
 * @file tmdq_core.cpp
 * @brief The flat C ABI over the decommutation pipeline, built with NO Qt.
 *
 * The same contract as dotnet/interop-spike/tmdq_abi.h (that header is used unchanged, so a
 * managed host written against it keeps working), implemented on the plain core the de-Qt sweep
 * produced: std::filesystem paths in, UTF-8 std::string out, and the pipeline itself - the reader,
 * the queues, the worker threads and the order they start and stop in - is `PipelineRunner`, the
 * same class the Qt application's `ProcessingCoordinator` wraps. This file only translates: a
 * request struct into a one-stream job, the run's reports into a captured error and result, and the
 * result into C arrays. (An earlier version of this file carried its own single-stream copy of the
 * orchestration, and a workaround for a reader left blocked on a dead worker's queue.)
 *
 * What this file does NOT contain, and the interop spike's version did: any Qt header, a
 * QCoreApplication, QElapsedTimer, QVector. The DLL it builds depends on the C runtime and the
 * Windows sockets library the vendored irig106 code calls - nothing else.
 */

#include "tmdq_abi.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>

#include "chapter10reader.h"
#include "framesetup.h"
#include "pipelinerunner.h"
#include "processedstreamdata.h"
#include "processingparams.h"

namespace {

/// Per-thread last error, so two hosts calling concurrently cannot overwrite each other's
/// message. Returned as a borrowed pointer, valid until the next call on the same thread.
thread_local std::string g_last_error;

void setError(const std::string& msg) { g_last_error = msg; }
void clearError() { g_last_error.clear(); }

/// Copies a std::vector<double> into a freshly malloc'd C array. An empty vector still yields a
/// valid 0-length allocation so the caller never has to special-case it.
double* copyToC(const std::vector<double>& src)
{
    const size_t n = src.size();
    double* out = static_cast<double*>(std::malloc((n ? n : 1) * sizeof(double)));
    if (!out)
    {
        return nullptr;
    }
    if (n)
    {
        std::memcpy(out, src.data(), n * sizeof(double));
    }
    return out;
}

/// What a one-stream run reports, kept for the caller: the stream's result if it earned one, and the
/// FIRST failure, so the host is told WHY a run failed instead of being given a guess. Called from
/// the runner's threads, so it is locked.
class RunCapture final : public PipelineEvents
{
public:
    void logMessage(const std::string&) override {}
    void errorOccurred(const std::string& message) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_first_error.empty())
        {
            m_first_error = message;
        }
    }
    void processingFinished(bool) override {}
    void streamProcessed(ProcessedStreamData data) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_result = std::move(data);
        m_have_result = true;
    }

    std::string firstError() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_first_error;
    }
    /// Moves the result out. Call after the runner has ended, when nothing can still be writing it.
    bool takeResult(ProcessedStreamData& out)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_have_result)
        {
            return false;
        }
        out = std::move(m_result);
        return true;
    }

private:
    mutable std::mutex  m_mutex;
    std::string         m_first_error;
    ProcessedStreamData m_result;
    bool                m_have_result = false;
};

} // namespace

int32_t tmdq_abi_version(void) { return TMDQ_ABI_VERSION; }
int32_t tmdq_sizeof_lock_request(void) { return static_cast<int32_t>(sizeof(tmdq_lock_request)); }
int32_t tmdq_sizeof_lock_result(void) { return static_cast<int32_t>(sizeof(tmdq_lock_result)); }
int32_t tmdq_sizeof_file_info(void) { return static_cast<int32_t>(sizeof(tmdq_file_info)); }

int32_t tmdq_probe_file(const char* filename_utf8, tmdq_file_info* out)
{
    clearError();
    if (!filename_utf8 || !out)
    {
        setError("null filename or output");
        return TMDQ_ERR_BADARG;
    }
    std::memset(out, 0, sizeof(*out));
    out->time_channel_id = -1;

    try
    {
        Chapter10Reader reader;
        // The host passes UTF-8; a path is built from it as char8_t so the conversion is the
        // standard's UTF-8 one, not the process code page's.
        const std::u8string utf8(reinterpret_cast<const char8_t*>(filename_utf8));
        // Concurrent hosts are safe: the reader serialises its own setup (irig106lock.h), because
        // the vendored irig106 library's handle table and TMATS decoder are not thread-safe.
        if (!reader.loadChannels(std::filesystem::path(utf8)))
        {
            setError(reader.lastError().empty()
                         ? std::string("Chapter10Reader::loadChannels failed - unreadable file or no TMATS")
                         : reader.lastError());
            return TMDQ_ERR_PREPARE;
        }

        out->time_channel_id = reader.getCurrentTimeChannelID();

        int written = 0;
        for (const auto& entry : reader.getPCMChannelList())
        {
            if (written >= TMDQ_MAX_PCM_CHANNELS)
            {
                break;  // reported truncated rather than overrunning the caller's array
            }
            out->pcm_channel_ids[written++] = entry.first;
        }
        out->pcm_channel_count = written;
        return TMDQ_OK;
    }
    catch (const std::exception& e)
    {
        setError(std::string("exception probing file: ") + e.what());
        return TMDQ_ERR_INTERNAL;
    }
    catch (...)
    {
        setError("unknown exception probing file");
        return TMDQ_ERR_INTERNAL;
    }
}

const char* tmdq_last_error(void) { return g_last_error.c_str(); }

void tmdq_free_lock_result(tmdq_lock_result* r)
{
    if (!r)
    {
        return;
    }
    std::free(r->times_sec);
    std::free(r->lock_percent);
    std::free(r->missed_frames);
    std::memset(r, 0, sizeof(*r));
}

int32_t tmdq_run_lock_stream(const tmdq_lock_request* req, tmdq_lock_result* out)
{
    clearError();

    if (!req || !out || !req->filename_utf8)
    {
        setError("null request, output or filename");
        return TMDQ_ERR_BADARG;
    }
    std::memset(out, 0, sizeof(*out));

    // Nothing below may let an exception reach the caller: unwinding across a C ABI into managed
    // code is undefined, and a crash here would be blamed on P/Invoke.
    try
    {
        if (req->sync_pattern_length <= 0 || req->bits_in_minor_frame <= 0 ||
            req->words_in_minor_frame <= 0 || req->sample_period_sec <= 0.0)
        {
            setError("sync_pattern_length, bits/words_in_minor_frame and "
                     "sample_period_sec must all be positive");
            return TMDQ_ERR_BADARG;
        }

        const std::u8string utf8(reinterpret_cast<const char8_t*>(req->filename_utf8));

        StreamJob job;
        ProcessingParams& params = job.params;
        params.filename           = std::filesystem::path(utf8);
        params.timeChannelId      = req->time_channel_id;
        params.pcmChannelId       = req->pcm_channel_id;
        params.frameSync          = req->frame_sync;
        params.frameSyncMask      = req->frame_sync_mask;
        params.syncPatternLength  = req->sync_pattern_length;
        params.wordsInMinorFrame  = req->words_in_minor_frame;
        params.bitsInMinorFrame   = req->bits_in_minor_frame;
        params.samplePeriodSec    = req->sample_period_sec;
        params.swapBytes          = req->swap_bytes   != 0;
        params.isRandomized       = req->is_randomized != 0;
        params.isInverted         = req->is_inverted  != 0;
        params.startSeconds       = 0;
        params.stopSeconds        = UINT64_MAX;
        params.mode               = StreamMode::FrameSyncLockStats;
        params.streamLabel        = "abi";

        // Lock-only mode reads no receiver words, so an empty FrameSetup is correct - the same
        // thing ProcessingCoordinator and the tests hand a lock-only job.
        job.frameSetup = std::make_shared<FrameSetup>();

        const auto capture = std::make_shared<RunCapture>();
        PipelineRunner runner;

        const auto started = std::chrono::steady_clock::now();

        // start() opens the recording and resolves its TMATS before any thread runs, so a file that
        // cannot be used is a prepare failure, reported with the reader's own reason. Concurrent
        // hosts are safe: the reader serialises its setup against irig106's unlocked global state.
        std::string start_error;
        if (!runner.start({job}, capture, start_error))
        {
            setError(start_error.empty() ? std::string("PipelineRunner::start failed") : start_error);
            return TMDQ_ERR_PREPARE;
        }
        runner.wait();   // every thread has ended, and the pipeline has been released, when this returns

        const double elapsed_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();

        ProcessedStreamData res;
        if (!capture->takeResult(res))
        {
            const std::string why = capture->firstError();
            setError(why.empty() ? std::string("the run produced no result") : why);
            return TMDQ_ERR_PROCESS;
        }
        if (res.timesSec.empty())
        {
            setError("the run completed but produced no output samples");
            return TMDQ_ERR_NOSAMPLES;
        }

        out->sample_count  = static_cast<int32_t>(res.timesSec.size());
        out->times_sec     = copyToC(res.timesSec);
        out->lock_percent  = copyToC(res.lockPercent);
        out->missed_frames = copyToC(res.accumulatedMissedFrames);
        out->elapsed_ms    = elapsed_ms;

        if (!out->times_sec || !out->lock_percent || !out->missed_frames)
        {
            tmdq_free_lock_result(out);
            setError("out of memory copying results across the boundary");
            return TMDQ_ERR_INTERNAL;
        }
        return TMDQ_OK;
    }
    catch (const std::exception& e)
    {
        tmdq_free_lock_result(out);
        setError(std::string("exception at the ABI boundary: ") + e.what());
        return TMDQ_ERR_INTERNAL;
    }
    catch (...)
    {
        tmdq_free_lock_result(out);
        setError("unknown exception at the ABI boundary");
        return TMDQ_ERR_INTERNAL;
    }
}
