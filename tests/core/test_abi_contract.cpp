// The shape of the C ABI's contract: the layout of every struct, the version and the revision, how a struct from a newer or
// older host is read, and the file functions (src/core/abi/tmdq_core.cpp and abi_file.cpp).

#include "test_abi_support.h"

// ---------------------------------------------------------------------------
// The shape of the contract
// ---------------------------------------------------------------------------

TEST_CASE("Abi: the struct layouts are pinned")
{
    // What the managed side must reproduce, field by field (Pack = 8, natural alignment). An offset moving
    // here is an ABI break, so it is a deliberate edit to this test and a new revision, never a side effect.
    CHECK(int(sizeof(tmdq_channel_info)) == 24);
    CHECK(int(offsetof(tmdq_channel_info, struct_size)) == 0);
    CHECK(int(offsetof(tmdq_channel_info, channel_id)) == 4);
    CHECK(int(offsetof(tmdq_channel_info, tmats_data_rate_bps)) == 8);
    CHECK(int(offsetof(tmdq_channel_info, name_utf8)) == 16);

    CHECK(int(sizeof(tmdq_cal_point)) == 16);
    CHECK(int(offsetof(tmdq_cal_point, true_db)) == 8);

    CHECK(int(sizeof(tmdq_cal_profile)) == 24);
    CHECK(int(offsetof(tmdq_cal_profile, word_index)) == 8);
    REQUIRE(int(offsetof(tmdq_cal_profile, point_count)) == 12);
    CHECK(int(offsetof(tmdq_cal_profile, valid)) == 16);

    CHECK(int(sizeof(tmdq_stream_config)) == 104);
    CHECK(int(offsetof(tmdq_stream_config, pcm_channel_id)) == 4);
    CHECK(int(offsetof(tmdq_stream_config, label_utf8)) == 8);
    CHECK(int(offsetof(tmdq_stream_config, sync_pattern_utf8)) == 16);
    CHECK(int(offsetof(tmdq_stream_config, sync_mask_utf8)) == 24);
    CHECK(int(offsetof(tmdq_stream_config, receiver_params_toml_utf8)) == 32);
    CHECK(int(offsetof(tmdq_stream_config, calibration)) == 40);
    CHECK(int(offsetof(tmdq_stream_config, data_rate_mbps)) == 48);
    CHECK(int(offsetof(tmdq_stream_config, scale_d_db_per_v)) == 56);
    CHECK(int(offsetof(tmdq_stream_config, mode)) == 64);
    CHECK(int(offsetof(tmdq_stream_config, bits_in_minor_frame)) == 68);
    CHECK(int(offsetof(tmdq_stream_config, randomized)) == 72);
    CHECK(int(offsetof(tmdq_stream_config, inverted)) == 76);
    CHECK(int(offsetof(tmdq_stream_config, sample_period_index)) == 80);
    CHECK(int(offsetof(tmdq_stream_config, polarity_index)) == 84);
    CHECK(int(offsetof(tmdq_stream_config, slope_index)) == 88);
    CHECK(int(offsetof(tmdq_stream_config, num_receivers)) == 92);
    CHECK(int(offsetof(tmdq_stream_config, receiver_channels)) == 96);
    REQUIRE(int(offsetof(tmdq_stream_config, calibration_count)) == 100);

    CHECK(int(sizeof(tmdq_run_request)) == 48);
    CHECK(int(offsetof(tmdq_run_request, time_channel_id)) == 4);
    CHECK(int(offsetof(tmdq_run_request, filename_utf8)) == 8);
    CHECK(int(offsetof(tmdq_run_request, app_root_utf8)) == 16);
    CHECK(int(offsetof(tmdq_run_request, streams)) == 24);
    REQUIRE(int(offsetof(tmdq_run_request, stream_count)) == 32);
    CHECK(int(offsetof(tmdq_run_request, source_id)) == 36);
    CHECK(int(offsetof(tmdq_run_request, swap_bytes)) == 40);

    CHECK(int(sizeof(tmdq_callbacks)) == 56);
    CHECK(int(offsetof(tmdq_callbacks, user)) == 8);
    CHECK(int(offsetof(tmdq_callbacks, log)) == 16);
    CHECK(int(offsetof(tmdq_callbacks, error)) == 24);
    CHECK(int(offsetof(tmdq_callbacks, progress)) == 32);
    CHECK(int(offsetof(tmdq_callbacks, stream_done)) == 40);
    CHECK(int(offsetof(tmdq_callbacks, finished)) == 48);

    CHECK(int(sizeof(tmdq_stream_info)) == 64);
    CHECK(int(offsetof(tmdq_stream_info, pcm_channel_id)) == 4);
    CHECK(int(offsetof(tmdq_stream_info, label_utf8)) == 8);
    CHECK(int(offsetof(tmdq_stream_info, times_sec)) == 16);
    CHECK(int(offsetof(tmdq_stream_info, lock_percent)) == 24);
    CHECK(int(offsetof(tmdq_stream_info, missed_frames)) == 32);
    CHECK(int(offsetof(tmdq_stream_info, job_index)) == 40);
    CHECK(int(offsetof(tmdq_stream_info, source_id)) == 44);
    CHECK(int(offsetof(tmdq_stream_info, mode)) == 48);
    REQUIRE(int(offsetof(tmdq_stream_info, sample_count)) == 52);
    REQUIRE(int(offsetof(tmdq_stream_info, channel_count)) == 56);

    CHECK(int(sizeof(tmdq_channel_series)) == 24);
    CHECK(int(offsetof(tmdq_channel_series, word_index)) == 4);
    CHECK(int(offsetof(tmdq_channel_series, name_utf8)) == 8);
    CHECK(int(offsetof(tmdq_channel_series, values)) == 16);

    CHECK(int(sizeof(tmdq_calibration_request)) == 112);
    CHECK(int(offsetof(tmdq_calibration_request, time_channel_id)) == 4);
    CHECK(int(offsetof(tmdq_calibration_request, cal_filename_utf8)) == 8);
    CHECK(int(offsetof(tmdq_calibration_request, app_root_utf8)) == 16);
    CHECK(int(offsetof(tmdq_calibration_request, sync_pattern_utf8)) == 24);
    CHECK(int(offsetof(tmdq_calibration_request, sync_mask_utf8)) == 32);
    CHECK(int(offsetof(tmdq_calibration_request, receiver_params_toml_utf8)) == 40);
    CHECK(int(offsetof(tmdq_calibration_request, step_db)) == 48);
    CHECK(int(offsetof(tmdq_calibration_request, data_rate_mbps)) == 56);
    CHECK(int(offsetof(tmdq_calibration_request, clip_start_sec)) == 64);
    CHECK(int(offsetof(tmdq_calibration_request, clip_end_sec)) == 72);
    CHECK(int(offsetof(tmdq_calibration_request, pcm_channel_id)) == 80);
    CHECK(int(offsetof(tmdq_calibration_request, bits_in_minor_frame)) == 84);
    CHECK(int(offsetof(tmdq_calibration_request, randomized)) == 88);
    CHECK(int(offsetof(tmdq_calibration_request, inverted)) == 92);
    CHECK(int(offsetof(tmdq_calibration_request, swap_bytes)) == 96);
    CHECK(int(offsetof(tmdq_calibration_request, num_receivers)) == 100);
    CHECK(int(offsetof(tmdq_calibration_request, receiver_channels)) == 104);
    REQUIRE(int(offsetof(tmdq_calibration_request, step_count)) == 108);

    CHECK(int(sizeof(tmdq_calibration_info)) == 48);
    REQUIRE(int(offsetof(tmdq_calibration_info, channel_count)) == 4);
    CHECK(int(offsetof(tmdq_calibration_info, summary_utf8)) == 8);
    CHECK(int(offsetof(tmdq_calibration_info, error_utf8)) == 16);
    CHECK(int(offsetof(tmdq_calibration_info, report_utf8)) == 24);
    CHECK(int(offsetof(tmdq_calibration_info, recording_seconds)) == 32);
    CHECK(int(offsetof(tmdq_calibration_info, clip_ignored)) == 40);

    CHECK(int(sizeof(tmdq_calibration_channel)) == 96);
    CHECK(int(offsetof(tmdq_calibration_channel, word_index)) == 4);
    CHECK(int(offsetof(tmdq_calibration_channel, name_utf8)) == 8);
    CHECK(int(offsetof(tmdq_calibration_channel, points)) == 16);
    CHECK(int(offsetof(tmdq_calibration_channel, plateau_levels)) == 24);
    CHECK(int(offsetof(tmdq_calibration_channel, unresolved_db)) == 32);
    CHECK(int(offsetof(tmdq_calibration_channel, edge_threshold)) == 40);
    REQUIRE(int(offsetof(tmdq_calibration_channel, point_count)) == 48);
    REQUIRE(int(offsetof(tmdq_calibration_channel, plateau_level_count)) == 52);
    REQUIRE(int(offsetof(tmdq_calibration_channel, unresolved_count)) == 56);
    CHECK(int(offsetof(tmdq_calibration_channel, valid)) == 60);
    CHECK(int(offsetof(tmdq_calibration_channel, had_data)) == 64);
    CHECK(int(offsetof(tmdq_calibration_channel, outcome)) == 68);
    CHECK(int(offsetof(tmdq_calibration_channel, detected_plateaus)) == 72);
    CHECK(int(offsetof(tmdq_calibration_channel, extra_plateaus)) == 76);
    CHECK(int(offsetof(tmdq_calibration_channel, threshold_relaxed)) == 80);
    CHECK(int(offsetof(tmdq_calibration_channel, saturated)) == 84);
    CHECK(int(offsetof(tmdq_calibration_channel, sweep_levels)) == 88);

    CHECK(tmdq_sizeof(TMDQ_STRUCT_CALIBRATION_REQUEST) == 112);
    CHECK(tmdq_sizeof(TMDQ_STRUCT_CALIBRATION_INFO) == 48);
    CHECK(tmdq_sizeof(TMDQ_STRUCT_CALIBRATION_CHANNEL) == 96);
    CHECK(int(TMDQ_STRUCT_CALIBRATION_REQUEST) == 9);
    CHECK(int(TMDQ_STRUCT_CALIBRATION_CHANNEL) == 11);
    CHECK(int(TMDQ_ERR_STATE) == 7);
    CHECK(int(TMDQ_CAL_CALIBRATED) == int(CalibrationOutcome::Calibrated));
    CHECK(int(TMDQ_CAL_NO_DATA) == int(CalibrationOutcome::NoData));
    CHECK(int(TMDQ_CAL_FLAT_NO_SIGNAL) == int(CalibrationOutcome::FlatNoSignal));
    CHECK(int(TMDQ_CAL_TOO_FEW_PLATEAUS) == int(CalibrationOutcome::TooFewPlateaus));
    CHECK(int(TMDQ_CAL_NO_MONOTONIC_SWEEP) == int(CalibrationOutcome::NoMonotonicSweep));

    CHECK(int(sizeof(tmdq_frame_sync_file_info)) == 48);
    CHECK(int(offsetof(tmdq_frame_sync_file_info, has_bits_per_frame)) == 4);
    CHECK(int(offsetof(tmdq_frame_sync_file_info, bits_per_frame)) == 8);
    CHECK(int(offsetof(tmdq_frame_sync_file_info, bits_from_legacy_key)) == 12);
    CHECK(int(offsetof(tmdq_frame_sync_file_info, inverted)) == 16);
    CHECK(int(offsetof(tmdq_frame_sync_file_info, has_randomized)) == 20);
    CHECK(int(offsetof(tmdq_frame_sync_file_info, randomized)) == 24);
    CHECK(int(offsetof(tmdq_frame_sync_file_info, pattern_utf8)) == 32);
    CHECK(int(offsetof(tmdq_frame_sync_file_info, mask_utf8)) == 40);

    CHECK(int(sizeof(tmdq_match_info)) == 32);
    CHECK(int(offsetof(tmdq_match_info, ok)) == 4);
    REQUIRE(int(offsetof(tmdq_match_info, missing_count)) == 8);
    REQUIRE(int(offsetof(tmdq_match_info, extra_count)) == 12);
    CHECK(int(offsetof(tmdq_match_info, missing)) == 16);
    CHECK(int(offsetof(tmdq_match_info, extra)) == 24);

    CHECK(int(sizeof(tmdq_csv_info)) == 32);
    REQUIRE(int(offsetof(tmdq_csv_info, series_count)) == 4);
    CHECK(int(offsetof(tmdq_csv_info, base_day)) == 8);
    CHECK(int(offsetof(tmdq_csv_info, skipped_rows)) == 12);
    CHECK(int(offsetof(tmdq_csv_info, base_time_offset)) == 16);
    CHECK(int(offsetof(tmdq_csv_info, x_max)) == 24);

    CHECK(int(sizeof(tmdq_csv_series_info)) == 80);
    CHECK(int(offsetof(tmdq_csv_series_info, metric_type)) == 4);
    CHECK(int(offsetof(tmdq_csv_series_info, name_utf8)) == 8);
    CHECK(int(offsetof(tmdq_csv_series_info, stream_label_utf8)) == 16);
    CHECK(int(offsetof(tmdq_csv_series_info, x_values)) == 24);
    CHECK(int(offsetof(tmdq_csv_series_info, y_values)) == 32);
    CHECK(int(offsetof(tmdq_csv_series_info, y_min)) == 40);
    CHECK(int(offsetof(tmdq_csv_series_info, y_max)) == 48);
    CHECK(int(offsetof(tmdq_csv_series_info, receiver_index)) == 56);
    CHECK(int(offsetof(tmdq_csv_series_info, channel_index)) == 60);
    CHECK(int(offsetof(tmdq_csv_series_info, stream_order)) == 64);
    CHECK(int(offsetof(tmdq_csv_series_info, source_id)) == 68);
    REQUIRE(int(offsetof(tmdq_csv_series_info, point_count)) == 72);

    CHECK(int(sizeof(tmdq_csv_column)) == 48);
    CHECK(int(offsetof(tmdq_csv_column, metric_type)) == 4);
    CHECK(int(offsetof(tmdq_csv_column, name_utf8)) == 8);
    CHECK(int(offsetof(tmdq_csv_column, x_values)) == 16);
    CHECK(int(offsetof(tmdq_csv_column, y_values)) == 24);
    CHECK(int(offsetof(tmdq_csv_column, source_id)) == 32);
    REQUIRE(int(offsetof(tmdq_csv_column, x_count)) == 36);
    REQUIRE(int(offsetof(tmdq_csv_column, y_count)) == 40);

    CHECK(tmdq_sizeof(TMDQ_STRUCT_FRAME_SYNC_FILE_INFO) == 48);
    CHECK(tmdq_sizeof(TMDQ_STRUCT_MATCH_INFO) == 32);
    CHECK(tmdq_sizeof(TMDQ_STRUCT_CSV_INFO) == 32);
    CHECK(tmdq_sizeof(TMDQ_STRUCT_CSV_SERIES_INFO) == 80);
    CHECK(tmdq_sizeof(TMDQ_STRUCT_CSV_COLUMN) == 48);
    CHECK(int(TMDQ_STRUCT_FRAME_SYNC_FILE_INFO) == 12);
    CHECK(int(TMDQ_STRUCT_CSV_COLUMN) == 16);

    // Revision 1.
    CHECK(int(sizeof(tmdq_receiver_params)) == 32);
    CHECK(int(offsetof(tmdq_receiver_params, polarity_index)) == 4);
    CHECK(int(offsetof(tmdq_receiver_params, slope_index)) == 8);
    CHECK(int(offsetof(tmdq_receiver_params, num_receivers)) == 12);
    CHECK(int(offsetof(tmdq_receiver_params, receiver_channels)) == 16);
    CHECK(int(offsetof(tmdq_receiver_params, scale_d_db_per_v)) == 24);
    CHECK(tmdq_sizeof(TMDQ_STRUCT_RECEIVER_PARAMS) == 32);
    CHECK(int(TMDQ_STRUCT_RECEIVER_PARAMS) == 22);

    // Revision 2.
    CHECK(int(sizeof(tmdq_chart_view)) == 72);
    CHECK(int(offsetof(tmdq_chart_view, x_lower)) == 8);
    CHECK(int(offsetof(tmdq_chart_view, x_upper)) == 16);
    CHECK(int(offsetof(tmdq_chart_view, y_lower)) == 24);
    CHECK(int(offsetof(tmdq_chart_view, y_upper)) == 32);
    CHECK(int(offsetof(tmdq_chart_view, left)) == 40);
    CHECK(int(offsetof(tmdq_chart_view, top)) == 48);
    CHECK(int(offsetof(tmdq_chart_view, width)) == 56);
    CHECK(int(offsetof(tmdq_chart_view, height)) == 64);
    CHECK(int(sizeof(tmdq_geometry_info)) == 24);
    CHECK(int(offsetof(tmdq_geometry_info, segment_count)) == 4);
    CHECK(int(offsetof(tmdq_geometry_info, segments)) == 8);
    CHECK(int(offsetof(tmdq_geometry_info, band_count)) == 16);
    CHECK(int(offsetof(tmdq_geometry_info, decimated)) == 20);
    CHECK(tmdq_sizeof(TMDQ_STRUCT_CHART_VIEW) == 72);
    CHECK(tmdq_sizeof(TMDQ_STRUCT_GEOMETRY_INFO) == 24);
    CHECK(int(TMDQ_STRUCT_CHART_VIEW) == 23);
    CHECK(int(TMDQ_STRUCT_GEOMETRY_INFO) == 24);
    CHECK(int(TMDQ_X_TO_PIXEL) == 0);
    CHECK(int(TMDQ_PIXEL_TO_X) == 1);
    CHECK(int(TMDQ_Y_TO_PIXEL) == 2);
    CHECK(int(TMDQ_PIXEL_TO_Y) == 3);
    CHECK(int(TMDQ_ERR_IO) == 8);
    CHECK(int(TMDQ_METRIC_SNR) == int(MetricType::SNR));
    CHECK(int(TMDQ_METRIC_FRAME_SYNC_LOCK) == int(MetricType::FrameSyncLock));
    CHECK(int(TMDQ_METRIC_ACCUMULATED_MISSED_FRAMES) == int(MetricType::AccumulatedMissedFrames));

    CHECK(int(sizeof(tmdq_series_set_info)) == 24);
    REQUIRE(int(offsetof(tmdq_series_set_info, series_count)) == 4);
    REQUIRE(int(offsetof(tmdq_series_set_info, elapsed_count)) == 8);
    CHECK(int(offsetof(tmdq_series_set_info, elapsed)) == 16);

    CHECK(int(sizeof(tmdq_series_spec)) == 72);
    CHECK(int(offsetof(tmdq_series_spec, metric_type)) == 4);
    CHECK(int(offsetof(tmdq_series_spec, name_utf8)) == 8);
    CHECK(int(offsetof(tmdq_series_spec, stream_label_utf8)) == 16);
    CHECK(int(offsetof(tmdq_series_spec, y_values)) == 24);
    CHECK(int(offsetof(tmdq_series_spec, y_min)) == 32);
    CHECK(int(offsetof(tmdq_series_spec, y_max)) == 40);
    CHECK(int(offsetof(tmdq_series_spec, receiver_index)) == 48);
    CHECK(int(offsetof(tmdq_series_spec, channel_index)) == 52);
    CHECK(int(offsetof(tmdq_series_spec, stream_order)) == 56);
    CHECK(int(offsetof(tmdq_series_spec, stream_sequence)) == 60);
    CHECK(int(offsetof(tmdq_series_spec, source_id)) == 64);
    REQUIRE(int(offsetof(tmdq_series_spec, y_count)) == 68);

    CHECK(int(sizeof(tmdq_stream_key)) == 24);
    CHECK(int(offsetof(tmdq_stream_key, source_id)) == 4);
    CHECK(int(offsetof(tmdq_stream_key, label_utf8)) == 8);
    CHECK(int(offsetof(tmdq_stream_key, channel)) == 16);

    CHECK(int(sizeof(tmdq_time_base_info)) == 24);
    CHECK(int(offsetof(tmdq_time_base_info, base_day)) == 4);
    CHECK(int(offsetof(tmdq_time_base_info, base_abs_seconds)) == 8);
    CHECK(int(offsetof(tmdq_time_base_info, base_time_offset)) == 16);

    CHECK(int(sizeof(tmdq_imported_series)) == 24);
    CHECK(int(offsetof(tmdq_imported_series, metric_type)) == 4);
    CHECK(int(offsetof(tmdq_imported_series, stream_label_utf8)) == 8);
    CHECK(int(offsetof(tmdq_imported_series, source_id)) == 16);
    CHECK(int(offsetof(tmdq_imported_series, stream_order)) == 20);

    CHECK(tmdq_sizeof(TMDQ_STRUCT_SERIES_SET_INFO) == 24);
    CHECK(tmdq_sizeof(TMDQ_STRUCT_SERIES_SPEC) == 72);
    CHECK(tmdq_sizeof(TMDQ_STRUCT_STREAM_KEY) == 24);
    CHECK(tmdq_sizeof(TMDQ_STRUCT_TIME_BASE_INFO) == 24);
    CHECK(tmdq_sizeof(TMDQ_STRUCT_IMPORTED_SERIES) == 24);
    CHECK(int(TMDQ_STRUCT_SERIES_SET_INFO) == 17);
    CHECK(int(TMDQ_STRUCT_SERIES_SPEC) == 18);
    CHECK(int(TMDQ_STRUCT_STREAM_KEY) == 19);
    CHECK(int(TMDQ_STRUCT_TIME_BASE_INFO) == 20);
    CHECK(int(TMDQ_STRUCT_IMPORTED_SERIES) == 21);

    // And the library reports the same sizes, for the managed side to compare with Marshal.SizeOf.
    CHECK(tmdq_sizeof(TMDQ_STRUCT_CHANNEL_INFO) == 24);
    CHECK(tmdq_sizeof(TMDQ_STRUCT_CAL_POINT) == 16);
    CHECK(tmdq_sizeof(TMDQ_STRUCT_CAL_PROFILE) == 24);
    CHECK(tmdq_sizeof(TMDQ_STRUCT_STREAM_CONFIG) == 104);
    CHECK(tmdq_sizeof(TMDQ_STRUCT_RUN_REQUEST) == 48);
    CHECK(tmdq_sizeof(TMDQ_STRUCT_CALLBACKS) == 56);
    CHECK(tmdq_sizeof(TMDQ_STRUCT_STREAM_INFO) == 64);
    CHECK(tmdq_sizeof(TMDQ_STRUCT_CHANNEL_SERIES) == 24);
    CHECK(tmdq_sizeof(0) == 0);
    CHECK(tmdq_sizeof(99) == 0);


    // The numbers a host writes in its own copies of the enums.
    CHECK(int(TMDQ_OK) == 0);
    CHECK(int(TMDQ_ERR_BADARG) == 1);
    CHECK(int(TMDQ_ERR_PREPARE) == 2);
    CHECK(int(TMDQ_ERR_PROCESS) == 3);
    CHECK(int(TMDQ_ERR_NOSAMPLES) == 4);
    CHECK(int(TMDQ_ERR_INTERNAL) == 5);
    CHECK(int(TMDQ_ERR_JOB) == 6);
    CHECK(int(TMDQ_STRUCT_CHANNEL_INFO) == 1);
    CHECK(int(TMDQ_STRUCT_CHANNEL_SERIES) == 8);
    CHECK(int(TMDQ_MODE_RECEIVER_CHANNEL_INFO) == 0);
    CHECK(int(TMDQ_MODE_FRAME_SYNC_LOCK) == 1);

    // The values a host writes in its own enums.
    CHECK(int(TMDQ_MODE_RECEIVER_CHANNEL_INFO) == int(StreamMode::ReceiverChannelInfo));
    CHECK(int(TMDQ_MODE_FRAME_SYNC_LOCK) == int(StreamMode::FrameSyncLockStats));
}

TEST_CASE("Abi: the version and revision are reported")
{
    CHECK(tmdq_abi_version() == 2);
    CHECK(tmdq_abi_revision() == 3);   // 1: the receiver-parameters file (4.1a); 2: the chart's geometry (4.3a); 3: a series' index
    CHECK(int(TMDQ_ABI_VERSION) == tmdq_abi_version());
    CHECK(int(TMDQ_ABI_REVISION) == tmdq_abi_revision());
    REQUIRE(tmdq_last_error() != nullptr);
}

TEST_CASE("Abi: a struct from a newer host is accepted and one from an older host is refused")
{
    const StepCal f = loadStepCal();

    // A host built against a LATER revision passes a longer struct: the fields this library knows are
    // read, and the rest is ignored. Here the request, its stream and the callbacks are all longer.
    struct BigStream { tmdq_stream_config c; char extra[24]; };
    struct BigRequest { tmdq_run_request r; char extra[40]; };
    struct BigCallbacks { tmdq_callbacks c; char extra[16]; };

    const Case c = lockCase(f);
    BigStream stream{};
    const tmdq_stream_config wired = c.wire();
    std::memcpy(&stream.c, &wired, sizeof(tmdq_stream_config));
    stream.c.struct_size = sizeof(BigStream);
    const tmdq_stream_config* streams[] = { &stream.c };

    Seen seen;
    BigCallbacks callbacks{};
    callbacks.c = seen.callbacks();
    callbacks.c.struct_size = sizeof(BigCallbacks);

    const std::string root = rootUtf8();
    BigRequest request{};
    request.r.struct_size     = sizeof(BigRequest);
    request.r.time_channel_id = f.timeId;
    request.r.filename_utf8   = f.path.c_str();
    request.r.app_root_utf8   = root.c_str();
    request.r.streams         = streams;
    request.r.stream_count    = 1;
    request.r.swap_bytes      = 1;

    tmdq_run* run = nullptr;
    REQUIRE(tmdq_run_start(&request.r, &callbacks.c, &run) == int(TMDQ_OK));
    RunGuard guard{run};
    REQUIRE_MESSAGE(seen.waitFinished(kTimeoutMs), seen.joined());
    CHECK(seen.finishedSuccess == 1);

    // A host built against an EARLIER revision passes a shorter struct, which this library cannot read
    // safely: it is refused, with the reason, not guessed at.
    tmdq_run_request shortRequest = request.r;
    shortRequest.struct_size = sizeof(tmdq_run_request) - 1;
    tmdq_run* none = nullptr;
    CHECK(tmdq_run_start(&shortRequest, &callbacks.c, &none) == int(TMDQ_ERR_BADARG));
    REQUIRE(none == nullptr);
    REQUIRE(std::string(tmdq_last_error()).find("struct_size") != std::string::npos);

    tmdq_stream_config shortStream = stream.c;
    shortStream.struct_size = sizeof(tmdq_stream_config) - 1;
    const tmdq_stream_config* shortStreams[] = { &shortStream };
    tmdq_run_request withShortStream = request.r;
    withShortStream.struct_size = sizeof(tmdq_run_request);
    withShortStream.streams = shortStreams;
    CHECK(tmdq_run_start(&withShortStream, &callbacks.c, &none) == int(TMDQ_ERR_BADARG));
    REQUIRE(std::string(tmdq_last_error()).find("stream 0") != std::string::npos);

    tmdq_callbacks shortCallbacks = seen.callbacks();
    shortCallbacks.struct_size = sizeof(tmdq_callbacks) - 1;
    withShortStream.streams = streams;
    CHECK(tmdq_run_start(&withShortStream, &shortCallbacks, &none) == int(TMDQ_ERR_BADARG));

    // A struct the LIBRARY fills in: a longer one is written no further than the library's own size,
    // and what lies beyond is left alone; a shorter one is refused.
    tmdq_file* file = nullptr;
    REQUIRE(tmdq_file_open(f.path.c_str(), &file) == int(TMDQ_OK));
    struct BigInfo { tmdq_channel_info i; unsigned char tail[16]; };
    BigInfo big;
    std::memset(&big, 0xAB, sizeof(big));
    big.i.struct_size = sizeof(BigInfo);
    REQUIRE(tmdq_file_pcm_channel(file, 0, &big.i) == int(TMDQ_OK));
    CHECK(big.i.channel_id == f.pcmId);
    for (unsigned char b : big.tail)
        CHECK(int(b) == 0xAB);
    tmdq_channel_info small{};
    small.struct_size = sizeof(small) - 1;
    CHECK(tmdq_file_pcm_channel(file, 0, &small) == int(TMDQ_ERR_BADARG));
    // Every getter says the same thing about a short or missing output (tmdq::writeOut).
    CHECK(std::string(tmdq_last_error()) == "null output or a struct_size that is too small");
    CHECK(tmdq_file_pcm_channel(file, 0, nullptr) == int(TMDQ_ERR_BADARG));
    CHECK(std::string(tmdq_last_error()) == "null output or a struct_size that is too small");
    tmdq_file_close(file);
}

// ---------------------------------------------------------------------------
// The file
// ---------------------------------------------------------------------------

TEST_CASE("Abi: a file lists its channels like the reader")
{
    for (const char* name : { "prn_testfile, sample.ch10", "step_cal, example.ch10" })
    {
        INFO(name);
        const std::string path = recording(name);
        Chapter10Reader reader;
        REQUIRE(reader.loadChannels(textfile::pathFromUtf8(path)));

        tmdq_file* file = nullptr;
        REQUIRE(tmdq_file_open(path.c_str(), &file) == int(TMDQ_OK));
        REQUIRE(file != nullptr);

        const auto pcm = reader.getPCMChannelList();
        const auto times = reader.getTimeChannelList();
        REQUIRE(tmdq_file_pcm_channel_count(file) == int(pcm.size()));
        REQUIRE(tmdq_file_time_channel_count(file) == int(times.size()));
        REQUIRE(pcm.size() >= 1);
        REQUIRE(times.size() >= 1);
        CHECK(tmdq_file_default_time_channel(file) == reader.getCurrentTimeChannelID());

        for (int i = 0; i < int(pcm.size()); ++i)
        {
            tmdq_channel_info info{};
            info.struct_size = sizeof(info);
            REQUIRE(tmdq_file_pcm_channel(file, i, &info) == int(TMDQ_OK));
            CHECK(info.channel_id == pcm[std::size_t(i)].first);
            CHECK(std::string(info.name_utf8) == pcm[std::size_t(i)].second);
            CHECK(info.tmats_data_rate_bps == reader.getTmatsDataRateBps(pcm[std::size_t(i)].first));
        }
        for (int i = 0; i < int(times.size()); ++i)
        {
            tmdq_channel_info info{};
            info.struct_size = sizeof(info);
            REQUIRE(tmdq_file_time_channel(file, i, &info) == int(TMDQ_OK));
            CHECK(info.channel_id == times[std::size_t(i)].first);
            CHECK(std::string(info.name_utf8) == times[std::size_t(i)].second);
            CHECK(info.tmats_data_rate_bps == 0.0);
        }
        tmdq_file_close(file);
    }

    // The step-cal recording declares an 800 kbit/s rate in its TMATS; that is not nothing.
    const std::string stepCal = recording("step_cal, example.ch10");
    tmdq_file* file = nullptr;
    REQUIRE(tmdq_file_open(stepCal.c_str(), &file) == int(TMDQ_OK));
    tmdq_channel_info info{};
    info.struct_size = sizeof(info);
    REQUIRE(tmdq_file_pcm_channel(file, 0, &info) == int(TMDQ_OK));
    CHECK(info.tmats_data_rate_bps == 800000.0);
    tmdq_file_close(file);
}

TEST_CASE("Abi: a file that cannot be opened says why")
{
    tmdq_file* file = reinterpret_cast<tmdq_file*>(std::uintptr_t{1});   // must be overwritten
    const std::string missing = tempUtf8() + "/tst_abi_no_such_recording.ch10";
    const std::string utf8 = missing;
    CHECK(tmdq_file_open(utf8.c_str(), &file) == int(TMDQ_ERR_PREPARE));
    REQUIRE(file == nullptr);
    REQUIRE_MESSAGE(std::string(tmdq_last_error()).size() > 5, tmdq_last_error());
    {
        // And the reason is the reader's own, word for word.
        Chapter10Reader reader;
        REQUIRE(!reader.loadChannels(textfile::pathFromUtf8(missing)));
        REQUIRE(!reader.lastError().empty());
        CHECK(std::string(tmdq_last_error()) == reader.lastError());
    }

    // A file that exists but is not a recording.
    Scratch dir;
    const std::string junk = dir.filePath("junk.ch10");
    writeFile(junk, std::string(4096, 'x'));
    CHECK(tmdq_file_open(junk.c_str(), &file) == int(TMDQ_ERR_PREPARE));
    REQUIRE(file == nullptr);
    REQUIRE(!std::string(tmdq_last_error()).empty());

    // The error belongs to the calling thread: another thread's success does not erase it.
    tmdq_file* other = nullptr;
    const std::string good = recording("step_cal, example.ch10");
    std::thread t([&] { tmdq_file_open(good.c_str(), &other); });
    t.join();
    REQUIRE(other != nullptr);
    REQUIRE(!std::string(tmdq_last_error()).empty());
    tmdq_file_close(other);
}

TEST_CASE("Abi: bad arguments to the file functions are refused")
{
    tmdq_file* file = reinterpret_cast<tmdq_file*>(std::uintptr_t{1});   // must be overwritten, even by a refusal
    CHECK(tmdq_file_open(nullptr, &file) == int(TMDQ_ERR_BADARG));
    REQUIRE(file == nullptr);
    CHECK(tmdq_file_open("whatever", nullptr) == int(TMDQ_ERR_BADARG));

    tmdq_file_close(nullptr);   // safe
    REQUIRE(tmdq_file_time_channel_count(nullptr) == -1);
    REQUIRE(tmdq_file_pcm_channel_count(nullptr) == -1);
    CHECK(tmdq_file_default_time_channel(nullptr) == -1);
    tmdq_channel_info info{};
    info.struct_size = sizeof(info);
    CHECK(tmdq_file_pcm_channel(nullptr, 0, &info) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_file_time_channel(nullptr, 0, &info) == int(TMDQ_ERR_BADARG));

    const std::string good = recording("step_cal, example.ch10");
    REQUIRE(tmdq_file_open(good.c_str(), &file) == int(TMDQ_OK));
    const int n = tmdq_file_pcm_channel_count(file);
    CHECK(tmdq_file_pcm_channel(file, n, &info) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_file_pcm_channel(file, -1, &info) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_file_pcm_channel(file, 0, nullptr) == int(TMDQ_ERR_BADARG));
    const int t = tmdq_file_time_channel_count(file);
    CHECK(tmdq_file_time_channel(file, t, &info) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_file_time_channel(file, -1, &info) == int(TMDQ_ERR_BADARG));
    tmdq_file_close(file);
}
