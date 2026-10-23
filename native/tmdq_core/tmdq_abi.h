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

#if defined(TMDQ_ABI_STATIC)
#  define TMDQ_API   /* compiled straight into a test executable, not loaded as a DLL */
#elif defined(_WIN32)
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

/// Bumped when functions or struct fields are ADDED (nothing existing changes meaning). A host that
/// needs a feature checks `version == TMDQ_ABI_VERSION && revision >= N`.
///   0  the interop spike's contract: probe a file, run one Frame Sync Lock stream.
///   1  plan step 2.2a: the file handle, and runs of N streams with callbacks and cancel.
///   2  plan step 2.2b: step files, and calibration extraction.
#define TMDQ_ABI_REVISION 2

typedef enum tmdq_status
{
    TMDQ_OK            = 0,
    TMDQ_ERR_BADARG    = 1,  ///< Null pointer, or a request field outside its range.
    TMDQ_ERR_PREPARE   = 2,  ///< The file would not open, or TMATS did not resolve.
    TMDQ_ERR_PROCESS   = 3,  ///< The run failed - usually no frame sync found.
    TMDQ_ERR_NOSAMPLES = 4,  ///< The run succeeded but produced no output samples.
    TMDQ_ERR_INTERNAL  = 5,  ///< An exception was caught at the boundary.
    TMDQ_ERR_JOB       = 6,  ///< A stream's (or a calibration's) settings were rejected; tmdq_last_error() says why.
    TMDQ_ERR_STATE     = 7   ///< The call is not valid yet: a result was asked for while its run is still going.
} tmdq_status;

/// How a stream is processed (the values of the core's StreamMode).
typedef enum tmdq_stream_mode
{
    TMDQ_MODE_RECEIVER_CHANNEL_INFO = 0,   ///< Calibrated receiver-channel (SNR) values.
    TMDQ_MODE_FRAME_SYNC_LOCK       = 1    ///< Frame-sync lock percentage only.
} tmdq_stream_mode;

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

/* ===========================================================================================
 * Revision 1: files and runs.
 *
 * CONVENTIONS FOR EVERYTHING BELOW
 *
 *  - Handles are opaque pointers, created by a function that returns them through an out
 *    parameter and released by the matching *_free / *_close. A handle is never shared between
 *    two owners.
 *  - A struct a HOST fills in begins with `uint32_t struct_size`, set to sizeof the struct as the
 *    host was compiled. Fields are only ever appended, so a library newer than the host reads the
 *    fields the host knows about and defaults the rest. A struct the LIBRARY fills in has the same
 *    first field, set by the library.
 *  - Strings in are UTF-8, NUL-terminated, copied by the library before the call returns. A NULL
 *    string means "not given". Strings out are UTF-8 and owned by the handle they came from.
 *  - Arrays out are views into the handle they came from (no copy), valid until it is released.
 *  - Nothing throws across the boundary. A failure returns a tmdq_status and sets
 *    tmdq_last_error() for the calling thread.
 *  - Text a function returns with no handle to own it is written into a buffer the caller provides,
 *    snprintf-style: the return value is the length needed (not counting the NUL), and a buffer that is
 *    too small receives a truncated, terminated copy. A negative return is an error.
 * ======================================================================================== */

/// Which struct tmdq_sizeof() measures.
typedef enum tmdq_struct_id
{
    TMDQ_STRUCT_CHANNEL_INFO   = 1,
    TMDQ_STRUCT_CAL_POINT      = 2,
    TMDQ_STRUCT_CAL_PROFILE    = 3,
    TMDQ_STRUCT_STREAM_CONFIG  = 4,
    TMDQ_STRUCT_RUN_REQUEST    = 5,
    TMDQ_STRUCT_CALLBACKS      = 6,
    TMDQ_STRUCT_STREAM_INFO    = 7,
    TMDQ_STRUCT_CHANNEL_SERIES = 8,
    TMDQ_STRUCT_CALIBRATION_REQUEST = 9,
    TMDQ_STRUCT_CALIBRATION_INFO    = 10,
    TMDQ_STRUCT_CALIBRATION_CHANNEL = 11
} tmdq_struct_id;

#pragma pack(push, 8)

/// One channel of a recording.
typedef struct tmdq_channel_info
{
    uint32_t    struct_size;
    int32_t     channel_id;
    double      tmats_data_rate_bps;  ///< PCM channels: the rate TMATS declares, 0 if it declares none. 0 for time channels.
    const char* name_utf8;            ///< The TMATS name, without the channel number. Owned by the file.
} tmdq_channel_info;

/// One measured calibration point: a raw count average and the true dB it stands for.
typedef struct tmdq_cal_point
{
    double raw_avg;
    double true_db;
} tmdq_cal_point;

/// A non-linear calibration profile for one receiver word.
typedef struct tmdq_cal_profile
{
    const tmdq_cal_point* points;     ///< point_count points, sorted ascending by raw_avg.
    int32_t               word_index; ///< Zero-based word within the minor frame.
    int32_t               point_count;
    int32_t               valid;      ///< 0/1. An invalid profile is ignored and the word is calibrated linearly.
    int32_t               reserved;
} tmdq_cal_profile;

/// One stream's settings, as the user gives them. The fields are those of the core's StreamJobConfig.
typedef struct tmdq_stream_config
{
    uint32_t    struct_size;
    int32_t     pcm_channel_id;
    const char* label_utf8;                 ///< Display label; NULL or empty = "Ch <id>".
    const char* sync_pattern_utf8;          ///< Frame-sync pattern as hex text.
    const char* sync_mask_utf8;             ///< Hex text; NULL or empty = every bit of the pattern.
    const char* receiver_params_toml_utf8;  ///< Receiver SNR: the word-map file; NULL or empty = the shipped default.
    const tmdq_cal_profile* calibration;    ///< calibration_count profiles (an array, stride sizeof(tmdq_cal_profile)); may be NULL. If two name the same word the first is used.
    double      data_rate_mbps;             ///< 0 = use the rate TMATS declares.
    double      scale_d_db_per_v;           ///< Receiver SNR: dB per volt.
    int32_t     mode;                       ///< tmdq_stream_mode.
    int32_t     bits_in_minor_frame;
    int32_t     randomized;                 ///< 0/1.
    int32_t     inverted;                   ///< 0/1.
    int32_t     sample_period_index;        ///< Index into the application's list of sample periods.
    int32_t     polarity_index;             ///< Receiver SNR.
    int32_t     slope_index;                ///< Receiver SNR.
    int32_t     num_receivers;              ///< Receiver SNR, used only when no word-map file is found.
    int32_t     receiver_channels;          ///< Receiver SNR, used only when no word-map file is found.
    int32_t     calibration_count;
} tmdq_stream_config;

/// One run: a recording, and the streams to process over it, concurrently, with one read of the file.
typedef struct tmdq_run_request
{
    uint32_t    struct_size;
    int32_t     time_channel_id;            ///< Required, >= 0 (see tmdq_file_default_time_channel).
    const char* filename_utf8;
    const char* app_root_utf8;              ///< Where settings/receiver_params/default.toml is looked for; NULL = none.
    const tmdq_stream_config* const* streams;   ///< stream_count pointers, each to a struct that carries its own struct_size.
    int32_t     stream_count;               ///< At least 1.
    int32_t     source_id;                  ///< Id of this file within a multi-file session; carried into every result.
    int32_t     swap_bytes;                 ///< 0/1, file-scoped. An ordinary Chapter 10 recording needs 1.
    int32_t     reserved;
} tmdq_run_request;

typedef struct tmdq_run tmdq_run;                       ///< A run in flight (or just ended).
typedef struct tmdq_stream_result tmdq_stream_result;   ///< One stream's finished result.

/// Where a run reports to. Any member may be NULL. The functions are called from the library's own
/// threads (see tmdq_run_start for which) and must not throw. A callback must NOT call tmdq_run_free
/// or tmdq_run_wait on its own run (they wait for the thread the callback is running on).
typedef struct tmdq_callbacks
{
    uint32_t struct_size;
    void*    user;                                                    ///< Passed back as the first argument of every call.
    void (*log)(void* user, const char* message_utf8);
    void (*error)(void* user, const char* message_utf8);
    void (*progress)(void* user, int32_t percent);                    ///< 0..100, from the reader thread only.
    /// A stream completed. Ownership of @p result passes to the host: release it with
    /// tmdq_stream_result_free (from any thread, at any time). Called only for a stream that
    /// completed successfully in a run that was not cancelled.
    void (*stream_done)(void* user, tmdq_stream_result* result);
    void (*finished)(void* user, int32_t success);                    ///< Once, last. success = at least one stream completed and no cancel.
} tmdq_callbacks;

/// A finished stream, as the library describes it. The arrays are views into the result, each
/// sample_count long and parallel.
typedef struct tmdq_stream_info
{
    uint32_t       struct_size;
    int32_t        pcm_channel_id;
    const char*    label_utf8;
    const double*  times_sec;           ///< Absolute IRIG seconds per sample.
    const double*  lock_percent;        ///< 0..100.
    const double*  missed_frames;       ///< Cumulative, monotonic.
    int32_t        job_index;           ///< Position of this stream in the request.
    int32_t        source_id;
    int32_t        mode;                ///< tmdq_stream_mode.
    int32_t        sample_count;
    int32_t        channel_count;       ///< Receiver series; 0 for a lock-only stream.
    int32_t        reserved;
} tmdq_stream_info;

/// One receiver-channel series of a finished stream.
typedef struct tmdq_channel_series
{
    uint32_t       struct_size;
    int32_t        word_index;          ///< Zero-based word within the minor frame.
    const char*    name_utf8;           ///< e.g. "L_RCVR1".
    const double*  values;              ///< sample_count of the stream; calibrated.
} tmdq_channel_series;

#pragma pack(pop)

/// @return TMDQ_ABI_REVISION as compiled into this DLL.
TMDQ_API int32_t tmdq_abi_revision(void);

/// @return The size, as the C compiler laid it out, of the struct @p which names; 0 for an unknown id.
TMDQ_API int32_t tmdq_sizeof(int32_t which);

/* --- the file --------------------------------------------------------------------------- */

typedef struct tmdq_file tmdq_file;   ///< A recording whose channels have been read.

/// Opens @p path_utf8 and reads its TMATS. Safe to call from several threads.
/// @return TMDQ_OK, TMDQ_ERR_BADARG or TMDQ_ERR_PREPARE (the file would not open; tmdq_last_error says why).
TMDQ_API int32_t tmdq_file_open(const char* path_utf8, tmdq_file** out);
TMDQ_API void    tmdq_file_close(tmdq_file* file);   ///< Safe on NULL.

TMDQ_API int32_t tmdq_file_time_channel_count(const tmdq_file* file);   ///< -1 for a NULL file.
TMDQ_API int32_t tmdq_file_pcm_channel_count(const tmdq_file* file);    ///< -1 for a NULL file.
/// The time channel the application selects first, or -1 if the file has none (or the file is NULL).
TMDQ_API int32_t tmdq_file_default_time_channel(const tmdq_file* file);
/// Fills @p out for the @p index'th time / PCM channel, in the order the application lists them.
/// @return TMDQ_OK, or TMDQ_ERR_BADARG for a NULL argument, an index out of range or a short struct_size.
TMDQ_API int32_t tmdq_file_time_channel(const tmdq_file* file, int32_t index, tmdq_channel_info* out);
TMDQ_API int32_t tmdq_file_pcm_channel(const tmdq_file* file, int32_t index, tmdq_channel_info* out);

/* --- runs ------------------------------------------------------------------------------- */

/// Builds a job for every stream (checking each, exactly as the application does), opens the
/// recording, and starts processing on the library's own threads; returns at once.
///
/// @p callbacks is copied. During this call `log` may be called, on the calling thread, with the
/// lines building the jobs produced. After a successful start: `log` and `error` come from the reader
/// thread or any stream's thread, in order per source; `progress` from the reader thread only;
/// `stream_done` from the thread of the stream it describes, after that stream's last log line;
/// `finished` from the reader thread, exactly once, LAST - after every other call and after every
/// thread has ended.
///
/// @return TMDQ_OK with *@p out set. TMDQ_ERR_BADARG for a NULL, an empty stream list or a short
/// struct_size; TMDQ_ERR_JOB when a stream's settings are rejected (the message names the stream and
/// the setting); TMDQ_ERR_PREPARE when the recording cannot be used. On any failure nothing is running,
/// *@p out is NULL and no callback but `log` has been called.
TMDQ_API int32_t tmdq_run_start(const tmdq_run_request* request, const tmdq_callbacks* callbacks, tmdq_run** out);

/// Asks the run to stop; it ends with `finished(0)`. Safe from any thread, including a callback.
TMDQ_API void    tmdq_run_cancel(tmdq_run* run);
/// 1 from a successful start until just before `finished` is delivered, else 0.
TMDQ_API int32_t tmdq_run_running(const tmdq_run* run);
/// Blocks until every thread of the run has ended (so every callback has returned). Not from a callback.
TMDQ_API void    tmdq_run_wait(tmdq_run* run);
/// Cancels a run still in flight, waits for it, and releases it. Safe on NULL. Not from a callback.
TMDQ_API void    tmdq_run_free(tmdq_run* run);

/// Describes @p result. @return TMDQ_OK, or TMDQ_ERR_BADARG for a NULL argument or a short struct_size.
TMDQ_API int32_t tmdq_stream_result_info(const tmdq_stream_result* result, tmdq_stream_info* out);
/// Describes the @p index'th receiver series of @p result. TMDQ_ERR_BADARG if @p index is out of range.
TMDQ_API int32_t tmdq_stream_result_channel(const tmdq_stream_result* result, int32_t index, tmdq_channel_series* out);
TMDQ_API void    tmdq_stream_result_free(tmdq_stream_result* result);   ///< Safe on NULL, and from any thread.

/* ===========================================================================================
 * Revision 2: step files and calibration extraction.
 * ======================================================================================== */

#pragma pack(push, 8)

/// How a channel's calibration came out (the core's CalibrationOutcome, numbered the same).
typedef enum tmdq_calibration_outcome
{
    TMDQ_CAL_CALIBRATED         = 0,   ///< A valid profile was built.
    TMDQ_CAL_NO_DATA            = 1,   ///< No samples for this channel (absent or disabled word).
    TMDQ_CAL_FLAT_NO_SIGNAL     = 2,   ///< Samples never moved: one level for the whole recording.
    TMDQ_CAL_TOO_FEW_PLATEAUS   = 3,   ///< Fewer confirmed plateaus than expected steps.
    TMDQ_CAL_NO_MONOTONIC_SWEEP = 4    ///< Enough plateaus, but no run of consecutive levels moving one way.
} tmdq_calibration_outcome;

/// One calibration to extract: the recording, the stream's settings as typed (which must be the values the
/// main run uses), the expected steps and the clips. The same fields the Extract Calibration dialog gathers.
typedef struct tmdq_calibration_request
{
    uint32_t    struct_size;
    int32_t     time_channel_id;
    const char* cal_filename_utf8;           ///< The calibration .ch10 file.
    const char* app_root_utf8;               ///< Where settings/receiver_params/default.toml is looked for; NULL = none.
    const char* sync_pattern_utf8;           ///< Hex text.
    const char* sync_mask_utf8;              ///< Hex text; NULL or empty = every bit of the pattern.
    const char* receiver_params_toml_utf8;   ///< NULL or empty = the shipped default word map, if the app root has one.
    const double* step_db;                   ///< step_count expected steps, in order (see tmdq_steps_parse).
    double      data_rate_mbps;              ///< 0 = use the TMATS rate.
    double      clip_start_sec;              ///< Seconds of the recording to ignore at the start.
    double      clip_end_sec;                ///< ... and at the end.
    int32_t     pcm_channel_id;
    int32_t     bits_in_minor_frame;
    int32_t     randomized;                  ///< 0/1.
    int32_t     inverted;                    ///< 0/1.
    int32_t     swap_bytes;                  ///< 0/1, file-scoped.
    int32_t     num_receivers;               ///< Sequential-grid fallback, used only when no word-map file is resolved.
    int32_t     receiver_channels;
    int32_t     step_count;
} tmdq_calibration_request;

/// What a finished calibration says overall.
typedef struct tmdq_calibration_info
{
    uint32_t    struct_size;
    int32_t     channel_count;
    const char* summary_utf8;      ///< The status line: "Calibrated N of M channel(s).", the failure's reason, or the cancel notice.
    const char* error_utf8;        ///< Why the run failed; empty on success and on a cancel.
    const char* report_utf8;       ///< The operator-facing report: which receivers calibrated and why the others did not.
    double      recording_seconds; ///< Length of the extracted recording.
    int32_t     clip_ignored;      ///< 0/1: the clips would have left nothing, so both were ignored.
    int32_t     reserved;
} tmdq_calibration_info;

/// One channel's outcome. The arrays are views into the calibration.
typedef struct tmdq_calibration_channel
{
    uint32_t              struct_size;
    int32_t               word_index;           ///< Zero-based word within the minor frame.
    const char*           name_utf8;            ///< e.g. "L_RCVR1".
    const tmdq_cal_point* points;               ///< The profile's points (point_count of them), sorted by raw_avg.
    const double*         plateau_levels;       ///< Settled averages of the plateaus found, in time order.
    const double*         unresolved_db;        ///< dB of every step no measured level could be paired with.
    double                edge_threshold;       ///< Raw-count edge threshold used to split plateaus.
    int32_t               point_count;
    int32_t               plateau_level_count;
    int32_t               unresolved_count;
    int32_t               valid;                ///< 0/1: the profile is usable. Pass such profiles to tmdq_run_start.
    int32_t               had_data;             ///< 0/1: any samples were extracted for this channel.
    int32_t               outcome;              ///< tmdq_calibration_outcome.
    int32_t               detected_plateaus;
    int32_t               extra_plateaus;       ///< 0/1: more plateaus than expected steps were found.
    int32_t               threshold_relaxed;    ///< 0/1: the nominal threshold could not resolve the sweep.
    int32_t               saturated;            ///< 0/1: the sweep ran off an end of the converter.
    int32_t               sweep_levels;
    int32_t               reserved;
} tmdq_calibration_channel;

#pragma pack(pop)

/* --- step files ------------------------------------------------------------------------- */

typedef struct tmdq_steps tmdq_steps;   ///< The expected steps read from a step-config TOML.

/// Parses an `[[Step]]` step-config file. @return TMDQ_OK, TMDQ_ERR_BADARG for a NULL, or TMDQ_ERR_PREPARE
/// when the file cannot be read or holds no valid step (tmdq_last_error has the parser's reason).
TMDQ_API int32_t       tmdq_steps_parse(const char* path_utf8, tmdq_steps** out);
TMDQ_API int32_t       tmdq_steps_count(const tmdq_steps* steps);   ///< -1 for NULL.
/// The step_count dB values, in file order; a view valid until tmdq_steps_free. NULL for NULL.
TMDQ_API const double* tmdq_steps_db(const tmdq_steps* steps);
TMDQ_API void          tmdq_steps_free(tmdq_steps* steps);          ///< Safe on NULL.

/// The step file the Extract Calibration dialog opens with: @p preferred if it names an existing file, else
/// `<app_root>/settings/rcvr_cals/default.toml` if that exists, else empty. Written into @p buffer as text
/// (see the conventions above); NULL strings read as empty.
TMDQ_API int32_t       tmdq_step_config_initial_path(const char* app_root_utf8, const char* preferred_utf8,
                                                     char* buffer, int32_t capacity);

/* --- calibration extraction ------------------------------------------------------------- */

typedef struct tmdq_calibration tmdq_calibration;   ///< An extraction in flight, or just ended.

/// Checks the request (decoding the sync text and resolving the word map exactly as the main run does) and
/// starts the extraction on the library's own threads; returns at once. @p callbacks is used as for a run,
/// except that `stream_done` is never called: `log` and `error` from the reader or worker or driver thread,
/// `progress` 0..100, and `finished` once, LAST. A failure the extractor can see before any work starts (no
/// steps, a word map it cannot use, a recording it cannot open) is reported through `error` then
/// `finished(0)` before this returns, and the handle is returned in its finished state.
/// @return TMDQ_OK with *@p out set; TMDQ_ERR_BADARG for a NULL, a short struct_size, no filename or a bad
/// step_count; TMDQ_ERR_JOB when the sync pattern or mask cannot be read ("Invalid frame sync pattern '...'.").
TMDQ_API int32_t tmdq_calibration_start(const tmdq_calibration_request* request, const tmdq_callbacks* callbacks,
                                        tmdq_calibration** out);
TMDQ_API void    tmdq_calibration_cancel(tmdq_calibration* calibration);          ///< Ends with finished(0).
TMDQ_API int32_t tmdq_calibration_running(const tmdq_calibration* calibration);  ///< 1 until just before `finished`.
TMDQ_API void    tmdq_calibration_wait(tmdq_calibration* calibration);           ///< Not from a callback.
TMDQ_API void    tmdq_calibration_free(tmdq_calibration* calibration);           ///< Cancels, waits, releases. Safe on NULL.

/// The overall outcome. Valid once `finished` has been delivered (or tmdq_calibration_wait returned);
/// TMDQ_ERR_STATE while the extraction is still running.
TMDQ_API int32_t tmdq_calibration_result(const tmdq_calibration* calibration, tmdq_calibration_info* out);
/// The @p index'th channel's outcome, in word order. TMDQ_ERR_STATE while running; TMDQ_ERR_BADARG for an
/// index out of range.
TMDQ_API int32_t tmdq_calibration_channel_result(const tmdq_calibration* calibration, int32_t index,
                                                 tmdq_calibration_channel* out);

#ifdef __cplusplus
}
#endif

#endif /* TMDQ_ABI_H */
