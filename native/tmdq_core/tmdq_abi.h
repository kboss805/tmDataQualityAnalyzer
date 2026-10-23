/**
 * @file tmdq_abi.h
 * @brief Flat C ABI over the decommutation pipeline - the boundary of tmdq_core.dll.
 *
 * WHAT THIS IS FOR
 *
 * The Avalonia application (docs/dotnet-migration/avalonia-plan.md) keeps the
 * performance-critical core as native MSVC C++ and puts the developer-facing surface in
 * C#. This header is the boundary that makes that possible:
 *
 *   1. The pipeline is driven through a flat C ABI - no Qt types, no C++ classes, no
 *      exceptions crossing.
 *   2. The struct layouts must agree between MSVC and the CLR. Get Pack wrong and you
 *      read plausible garbage at the wrong offsets, which is exactly the class of
 *      silent bug this project's domain invariants exist to catch - except here it
 *      would sit at a seam no other test looks at. Hence tmdq_sizeof_* below, so the
 *      managed side can ASSERT agreement rather than assume it.
 *   3. The boundary is cheap. Only RESULTS cross: flat double arrays, worst realistic
 *      case ~34 MB once per run against a 640 MB recording that is read and scanned
 *      entirely on this side.
 *
 * STATE: this is the interop spike's version-1 contract, unchanged - one Frame Sync
 * Lock stream per call. Plan step 2.2 grows it to everything the application needs.
 * The spike (dotnet/interop-spike) keeps its own copy of this header and stays frozen
 * at version 1 as the record of what was measured.
 *
 * CONVENTIONS
 *
 * - Strings in are UTF-8, NUL-terminated, owned by the caller for the duration.
 * - Results are allocated by this library and must be released with
 *   tmdq_free_lock_result(). The managed side copies out, then frees - no pinning.
 * - No function throws. Failures return a tmdq_status and set tmdq_last_error().
 * - tmdq_last_error() is per-thread.
 */

#ifndef TMDQ_ABI_H
#define TMDQ_ABI_H

#include <stdint.h>

#if defined(_WIN32)
#  if defined(TMDQ_ABI_BUILD)
#    define TMDQ_API __declspec(dllexport)
#  else
#    define TMDQ_API __declspec(dllimport)
#  endif
#else
#  define TMDQ_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/// Bumped on any incompatible change to the structs or the function set. The managed
/// side checks it before doing anything else, so a stale DLL fails loudly instead of
/// being misread.
#define TMDQ_ABI_VERSION 1

typedef enum tmdq_status
{
    TMDQ_OK            = 0,
    TMDQ_ERR_BADARG    = 1,  ///< Null pointer, or a request field outside its range.
    TMDQ_ERR_PREPARE   = 2,  ///< The file would not open, or TMATS did not resolve.
    TMDQ_ERR_PROCESS   = 3,  ///< The run failed - usually no frame sync found.
    TMDQ_ERR_NOSAMPLES = 4,  ///< The run succeeded but produced no output samples.
    TMDQ_ERR_INTERNAL  = 5   ///< An exception was caught at the boundary.
} tmdq_status;

/* Explicit packing on both sides of the boundary. x64 MSVC and the CLR both default to
 * natural alignment, so this changes nothing today - it is stated so that a future
 * change to either side is a compile-time decision rather than a silent offset shift. */
#pragma pack(push, 8)

/// One Frame Sync Lock stream to process. Mirrors the subset of ProcessingParams that a
/// lock-only run actually reads; everything else is defaulted on the native side.
typedef struct tmdq_lock_request
{
    const char* filename_utf8;        ///< Path to the .ch10 file, UTF-8.
    int32_t     time_channel_id;
    int32_t     pcm_channel_id;
    uint64_t    frame_sync;           ///< Sync pattern as a numeric value.
    uint64_t    frame_sync_mask;      ///< 0 = derive all-ones from sync_pattern_length.
    int32_t     sync_pattern_length;  ///< Bits.
    int32_t     words_in_minor_frame;
    int32_t     bits_in_minor_frame;
    double      sample_period_sec;    ///< Output sample period.
    int32_t     swap_bytes;           ///< 0/1. An ordinary Chapter 10 recording needs 1.
    int32_t     is_randomized;        ///< 0/1. RNRZ-L.
    int32_t     is_inverted;          ///< 0/1.
} tmdq_lock_request;

/// Output of one run. The three arrays are parallel and each holds sample_count
/// doubles. Owned by the library until tmdq_free_lock_result().
typedef struct tmdq_lock_result
{
    int32_t sample_count;
    double* times_sec;      ///< Absolute IRIG seconds per sample.
    double* lock_percent;   ///< Frame-sync lock percentage, 0..100.
    double* missed_frames;  ///< Cumulative missed frames (monotonic).
    double  elapsed_ms;     ///< Wall time of the native run, excluding marshalling.
} tmdq_lock_result;

/// Upper bound on channels tmdq_probe_file() will report. A fixed inline array is the
/// simplest blittable shape across the boundary - no ownership question, and it
/// exercises MarshalAs(ByValArray) on the managed side, which is a pattern any real
/// shell will need. The application itself enforces no channel cap; a file with more
/// than this many PCM channels is reported truncated, with pcm_channel_count set to
/// what was actually written.
#define TMDQ_MAX_PCM_CHANNELS 32

/// What a .ch10 file offers, so a host can pick a stream without reimplementing TMATS.
typedef struct tmdq_file_info
{
    int32_t time_channel_id;                                ///< -1 if none resolved.
    int32_t pcm_channel_count;                              ///< Entries written below.
    int32_t pcm_channel_ids[TMDQ_MAX_PCM_CHANNELS];
} tmdq_file_info;

#pragma pack(pop)

/// @return TMDQ_ABI_VERSION as compiled into this DLL.
TMDQ_API int32_t tmdq_abi_version(void);

/// Struct sizes as the C compiler laid them out, for the managed side to assert
/// against Marshal.SizeOf. The whole point of the spike's layout check.
TMDQ_API int32_t tmdq_sizeof_lock_request(void);
TMDQ_API int32_t tmdq_sizeof_lock_result(void);
TMDQ_API int32_t tmdq_sizeof_file_info(void);

/// Reads @p filename_utf8's TMATS and reports its time channel and PCM channels.
/// Cheap - metadata only, it does not walk the packet stream.
TMDQ_API int32_t tmdq_probe_file(const char* filename_utf8, tmdq_file_info* out);

/// Reads @p req->filename_utf8 once and processes the one PCM channel in Frame Sync
/// Lock mode, synchronously. Fills @p out on TMDQ_OK.
TMDQ_API int32_t tmdq_run_lock_stream(const tmdq_lock_request* req, tmdq_lock_result* out);

/// Releases the arrays in @p r and zeroes it. Safe on an all-zero struct.
TMDQ_API void tmdq_free_lock_result(tmdq_lock_result* r);

/// Last error on the calling thread, UTF-8, owned by the library. Empty if none.
TMDQ_API const char* tmdq_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* TMDQ_ABI_H */
