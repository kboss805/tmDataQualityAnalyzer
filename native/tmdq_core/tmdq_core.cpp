/**
 * @file tmdq_core.cpp
 * @brief The per-thread error, the version, and the struct sizes of the C ABI (tmdq_abi.h).
 *
 * The functions themselves are in the other files beside this one: abi_file.cpp, abi_run.cpp,
 * abi_calibration.cpp, abi_formats.cpp, abi_series.cpp. All of them are plain translation onto the Qt-free
 * core (std::filesystem paths in, UTF-8 std::string out). The DLL this builds (tmdq_core.vcxproj, which has
 * no Qt on its include path) depends on the C runtime and the Windows libraries the vendored irig106 code
 * calls - nothing else, and scripts\build_core_dll.ps1 fails the build if that changes.
 */

#include "tmdq_abi.h"

#include "abi_internal.h"

#include <string>

namespace tmdq {

/// Per-thread last error, so two hosts calling concurrently cannot overwrite each other's
/// message. Returned as a borrowed pointer, valid until the next call on the same thread.
thread_local std::string g_last_error;

void setError(const std::string& msg) { g_last_error = msg; }
void clearError() { g_last_error.clear(); }

} // namespace tmdq

int32_t tmdq_abi_version(void) { return TMDQ_ABI_VERSION; }
int32_t tmdq_abi_revision(void) { return TMDQ_ABI_REVISION; }

int32_t tmdq_sizeof(int32_t which)
{
    switch (which)
    {
        case TMDQ_STRUCT_CHANNEL_INFO:   return static_cast<int32_t>(sizeof(tmdq_channel_info));
        case TMDQ_STRUCT_CAL_POINT:      return static_cast<int32_t>(sizeof(tmdq_cal_point));
        case TMDQ_STRUCT_CAL_PROFILE:    return static_cast<int32_t>(sizeof(tmdq_cal_profile));
        case TMDQ_STRUCT_STREAM_CONFIG:  return static_cast<int32_t>(sizeof(tmdq_stream_config));
        case TMDQ_STRUCT_RUN_REQUEST:    return static_cast<int32_t>(sizeof(tmdq_run_request));
        case TMDQ_STRUCT_CALLBACKS:      return static_cast<int32_t>(sizeof(tmdq_callbacks));
        case TMDQ_STRUCT_STREAM_INFO:    return static_cast<int32_t>(sizeof(tmdq_stream_info));
        case TMDQ_STRUCT_CHANNEL_SERIES: return static_cast<int32_t>(sizeof(tmdq_channel_series));
        case TMDQ_STRUCT_CALIBRATION_REQUEST: return static_cast<int32_t>(sizeof(tmdq_calibration_request));
        case TMDQ_STRUCT_CALIBRATION_INFO:    return static_cast<int32_t>(sizeof(tmdq_calibration_info));
        case TMDQ_STRUCT_CALIBRATION_CHANNEL: return static_cast<int32_t>(sizeof(tmdq_calibration_channel));
        case TMDQ_STRUCT_FRAME_SYNC_FILE_INFO: return static_cast<int32_t>(sizeof(tmdq_frame_sync_file_info));
        case TMDQ_STRUCT_MATCH_INFO:          return static_cast<int32_t>(sizeof(tmdq_match_info));
        case TMDQ_STRUCT_CSV_INFO:            return static_cast<int32_t>(sizeof(tmdq_csv_info));
        case TMDQ_STRUCT_CSV_SERIES_INFO:     return static_cast<int32_t>(sizeof(tmdq_csv_series_info));
        case TMDQ_STRUCT_CSV_COLUMN:          return static_cast<int32_t>(sizeof(tmdq_csv_column));
        case TMDQ_STRUCT_SERIES_SET_INFO:     return static_cast<int32_t>(sizeof(tmdq_series_set_info));
        case TMDQ_STRUCT_SERIES_SPEC:         return static_cast<int32_t>(sizeof(tmdq_series_spec));
        case TMDQ_STRUCT_STREAM_KEY:          return static_cast<int32_t>(sizeof(tmdq_stream_key));
        case TMDQ_STRUCT_TIME_BASE_INFO:      return static_cast<int32_t>(sizeof(tmdq_time_base_info));
        case TMDQ_STRUCT_IMPORTED_SERIES:     return static_cast<int32_t>(sizeof(tmdq_imported_series));
        case TMDQ_STRUCT_RECEIVER_PARAMS:     return static_cast<int32_t>(sizeof(tmdq_receiver_params));
        default:                         return 0;
    }
}

const char* tmdq_last_error(void) { return tmdq::g_last_error.c_str(); }
