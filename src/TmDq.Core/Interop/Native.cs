using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

// The runtime's own resolver (Library.cs) loads the DLL by its full path before this search is ever used; the
// attribute says what a bare-name lookup would be allowed to do if it were: look in this assembly's folder only.
[assembly: DefaultDllImportSearchPaths(DllImportSearchPath.AssemblyDirectory)]

namespace TmDq.Core;

internal static unsafe partial class Native
{
    /// <summary>The library's name as every DllImport and the resolver in Library.cs spell it.</summary>
    internal const string Dll = "tmdq_core";

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_abi_version();

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_abi_revision();

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_sizeof(int which);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial byte* tmdq_last_error();

    // --- the file
    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_file_open(byte* pathUtf8, out IntPtr file);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_file_close(IntPtr file);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_file_time_channel_count(IntPtr file);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_file_pcm_channel_count(IntPtr file);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_file_default_time_channel(IntPtr file);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_file_time_channel(IntPtr file, int index, ChannelInfoNative* info);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_file_pcm_channel(IntPtr file, int index, ChannelInfoNative* info);

    // --- runs
    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_run_start(RunRequestNative* request, CallbacksNative* callbacks, out RunHandle run);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_run_cancel(RunHandle run);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_run_running(RunHandle run);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_run_wait(RunHandle run);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_run_free(IntPtr run);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_stream_result_info(StreamResultHandle result, StreamInfoNative* info);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_stream_result_channel(StreamResultHandle result, int index, ChannelSeriesNative* series);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_stream_result_free(IntPtr result);

    // --- step files and calibration
    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_steps_parse(byte* pathUtf8, out IntPtr steps);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_steps_count(IntPtr steps);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial double* tmdq_steps_db(IntPtr steps);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_steps_free(IntPtr steps);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_step_config_initial_path(byte* appRootUtf8, byte* preferredUtf8, byte* buffer, int capacity);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_calibration_start(CalibrationRequestNative* request, CallbacksNative* callbacks, out CalibrationRunHandle calibration);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_calibration_cancel(CalibrationRunHandle calibration);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_calibration_running(CalibrationRunHandle calibration);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_calibration_wait(CalibrationRunHandle calibration);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_calibration_free(IntPtr calibration);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_calibration_result(CalibrationRunHandle calibration, CalibrationInfoNative* info);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_calibration_channel_result(CalibrationRunHandle calibration, int index, CalibrationChannelNative* channel);

    // --- the formats
    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_frame_sync_file_read(byte* pathUtf8, out IntPtr file);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_frame_sync_file_info_get(IntPtr file, FrameSyncFileInfoNative* info);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_frame_sync_file_free(IntPtr file);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_frame_sync_file_write(byte* pathUtf8, byte* patternUtf8, byte* maskUtf8, int bitsPerFrame, int inverted, int randomized);
    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_receiver_params_read(byte* pathUtf8, ReceiverParamsNative* defaults, ReceiverParamsNative* result);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_receiver_params_save(byte* pathUtf8, ReceiverParamsNative* parameters, byte* wordMapSourceUtf8, int numWordsInMinorFrame);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_words_in_minor_frame(int bitsInMinorFrame);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_template_match(int* templateIds, int templateCount, int* fileIds, int fileCount, out IntPtr match);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_match_info_get(IntPtr match, MatchInfoNative* info);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_match_free(IntPtr match);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_csv_parse(byte* pathUtf8, out IntPtr csv);

    // a phased array antenna's SNR log, read into the same result a CSV is (revision 4)
    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_phased_array_parse(byte* pathUtf8, out IntPtr csv);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_csv_info_get(IntPtr csv, CsvInfoNative* info);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_csv_series_get(IntPtr csv, int index, CsvSeriesInfoNative* info);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_csv_free(IntPtr csv);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_csv_write(byte* pathUtf8, CsvColumnNative** columns, int count, int baseDay, double baseTimeOffset);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_clock_time_format(int baseDay, double baseTimeOffset, double elapsed, int includeMilliseconds, byte* buffer, int capacity);

    // --- series construction
    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_stream_result_series(StreamResultHandle result, double baseAbsSeconds, out SeriesSetHandle set);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_series_set_info_get(SeriesSetHandle set, SeriesSetInfoNative* info);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_series_set_spec(SeriesSetHandle set, int index, SeriesSpecNative* spec);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_series_set_free(IntPtr set);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_stream_result_key(StreamResultHandle result, StreamKeyNative* key);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_stream_key_equal(StreamKeyNative* a, StreamKeyNative* b);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_time_base_create(out TimeBaseHandle timeBase);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_time_base_free(IntPtr timeBase);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_time_base_info_get(TimeBaseHandle timeBase, TimeBaseInfoNative* info);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_time_base_reset(TimeBaseHandle timeBase);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_time_base_set_clock(TimeBaseHandle timeBase, int day, double offset);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_time_base_overlaps(TimeBaseHandle timeBase, double newMinAbs, double newMaxAbs, double existingMaxElapsed, out int overlaps);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_time_base_adopt(TimeBaseHandle timeBase, double firstAbsSeconds, int hasSeries, out double shift);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_time_base_advance(TimeBaseHandle timeBase, int hasEarliest, double earliestElapsed, out double shift);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_assign_imported_stream_orders(ImportedSeriesNative** series, int count);

    // --- the chart's geometry (revision 2)
    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_geometry_info_get(IntPtr geometry, GeometryInfoNative* info);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_geometry_band(IntPtr geometry, int index, out double* points, out int pointCount);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_geometry_free(IntPtr geometry);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_chart_sorted_x(double* xs, int count);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_chart_usable_range(double lower, double upper);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_chart_transform(ChartViewNative* view, int kind, double value, out double result);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_chart_ticks(double lower, double upper, int count, double* ticks, int capacity);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_chart_round_ticks(double lower, double upper, int count, double* ticks, int capacity);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_chart_time_ticks(double lower, double upper, int count, double origin, double* ticks, int capacity);

    // --- a series' index (revision 3)
    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_series_index_build(double* ys, int count, out SeriesIndexHandle index);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_series_index_count(SeriesIndexHandle index);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial void tmdq_series_index_free(IntPtr index);

    [LibraryImport(Dll)]
    [UnmanagedCallConv(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static partial int tmdq_chart_geometry_indexed(double* xs, double* ys, int count, int sortedX, SeriesIndexHandle index, ChartViewNative* view, out IntPtr geometry);
}
