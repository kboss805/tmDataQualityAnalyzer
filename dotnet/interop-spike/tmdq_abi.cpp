/**
 * @file tmdq_abi.cpp
 * @brief Implementation of the flat C ABI. See tmdq_abi.h for what the spike proves.
 *
 * The driving sequence mirrors ProcessingCoordinator for the single-stream case, and is
 * the same shape tests/tst_frameprocessor.cpp's runWithReader() helper uses: resolve
 * TMATS once, run the reader on its own thread, consume on the calling thread.
 *
 * Two deliberate departures from the Qt application:
 *
 *   - **std::thread, not QtConcurrent.** QtConcurrent would pull in the Qt global
 *     thread pool, and this DLL is called from a host that has no QCoreApplication.
 *     std::thread avoids that question entirely and is a step the de-Qt work needs
 *     anyway.
 *   - **No QCoreApplication is created.** Qt Core value types and QObject work without
 *     one; what does not work is anything needing an event loop, and nothing on this
 *     path does - process() is synchronous and the signals have no receivers.
 */

// TMDQ_ABI_BUILD (which flips TMDQ_API to dllexport) comes from the .pro's DEFINES,
// so it is NOT defined here - doing both is a C4005 macro redefinition, and this
// project gates on zero warnings.
#include "tmdq_abi.h"

#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include <QByteArray>
#include <QElapsedTimer>
#include <QString>
#include <QVector>

#include "ch10packetreader.h"
#include "chapter10reader.h"
#include "frameprocessor.h"
#include "framesetup.h"
#include "packetqueue.h"
#include "processedstreamdata.h"
#include "processingparams.h"

namespace {

/// Per-thread last error, so two hosts calling concurrently cannot overwrite each
/// other's message. Returned as a borrowed pointer, valid until the next call on the
/// same thread.
thread_local std::string g_last_error;

void setError(const std::string& msg)
{
    g_last_error = msg;
}

void clearError()
{
    g_last_error.clear();
}

/// Copies a QVector<double> into a freshly allocated C array. Returns nullptr only on
/// allocation failure; an empty vector still yields a valid 0-length allocation so the
/// caller never has to special-case it.
double* copyToC(const QVector<double>& src)
{
    const size_t n = static_cast<size_t>(src.size());
    double* out = static_cast<double*>(std::malloc((n ? n : 1) * sizeof(double)));
    if (!out)
    {
        return nullptr;
    }
    if (n)
    {
        std::memcpy(out, src.constData(), n * sizeof(double));
    }
    return out;
}

} // namespace

int32_t tmdq_abi_version(void)
{
    return TMDQ_ABI_VERSION;
}

int32_t tmdq_sizeof_lock_request(void)
{
    return static_cast<int32_t>(sizeof(tmdq_lock_request));
}

int32_t tmdq_sizeof_lock_result(void)
{
    return static_cast<int32_t>(sizeof(tmdq_lock_result));
}

int32_t tmdq_sizeof_file_info(void)
{
    return static_cast<int32_t>(sizeof(tmdq_file_info));
}

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
        if (!reader.loadChannels(QString::fromUtf8(filename_utf8)))
        {
            setError("Chapter10Reader::loadChannels failed - unreadable file or no TMATS");
            return TMDQ_ERR_PREPARE;
        }

        out->time_channel_id = reader.getCurrentTimeChannelID();

        const QList<QPair<int, QString>> pcm = reader.getPCMChannelList();
        int written = 0;
        for (const auto& entry : pcm)
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

const char* tmdq_last_error(void)
{
    return g_last_error.c_str();
}

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

    // Nothing below may let an exception reach the caller: unwinding across a C ABI
    // into managed code is undefined, and a crash here would be blamed on P/Invoke.
    try
    {
        if (req->sync_pattern_length <= 0 || req->bits_in_minor_frame <= 0 ||
            req->words_in_minor_frame <= 0 || req->sample_period_sec <= 0.0)
        {
            setError("sync_pattern_length, bits/words_in_minor_frame and "
                     "sample_period_sec must all be positive");
            return TMDQ_ERR_BADARG;
        }

        ProcessingParams params;
        params.filename           = QString::fromUtf8(req->filename_utf8);
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
        params.streamLabel        = QStringLiteral("abi-spike");

        // Lock-only mode reads no receiver words, so an empty FrameSetup is correct -
        // the same thing ProcessingCoordinator and the tests hand a lock-only job.
        FrameSetup setup(nullptr);

        PacketQueue queue;
        params.packetQueue = &queue;

        Ch10PacketReader reader;
        QVector<ProcessingParams*> params_list = { &params };
        QString prepare_error;
        if (!reader.prepare(params.filename, params.timeChannelId, params_list,
                            prepare_error))
        {
            setError(prepare_error.isEmpty()
                         ? std::string("Ch10PacketReader::prepare failed")
                         : prepare_error.toUtf8().constData());
            return TMDQ_ERR_PREPARE;
        }

        FrameProcessor processor;

        QElapsedTimer timer;
        timer.start();

        // The reader fills the queue; process() drains it on this thread. Joined before
        // anything is destroyed - a reader still running when its queue goes out of
        // scope is the hazard, and it is why the benchmark had to learn to drain.
        std::thread reader_thread([&reader]() { reader.run(); });
        const bool ok = processor.process(params, &setup);
        reader_thread.join();

        const double elapsed_ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;

        if (!ok)
        {
            setError("FrameProcessor::process reported failure - most often the frame "
                     "sync pattern was not found with these parameters");
            return TMDQ_ERR_PROCESS;
        }

        const ProcessedStreamData& res = processor.result();
        if (res.timesSec.isEmpty())
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
