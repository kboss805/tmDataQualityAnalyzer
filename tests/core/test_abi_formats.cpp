// The file formats through the C ABI (src/core/abi/abi_formats.cpp): the frame sync file, template matching, the CSV,
// clock stamps, and the receiver parameters file (revision 1), each read and written like the core does.

#include "receiverparamsfile.h"
#include "test_abi_support.h"

TEST_CASE("Abi: a frame sync file is read like the core")
{
    Scratch dir;
    std::vector<std::string> paths;
    for (const char* name : { "framesync_PRN11.toml", "framesync_PRN15.toml", "framesync_rcvr_default.toml" })
        paths.push_back(rootUtf8() + "/settings/framesync_patterns/" + name);
    // Each way a file can be odd.
    const std::vector<std::pair<const char*, std::string>> odd = {
        {"legacy", "[Frame]\nFrameSync = \"FE6B2840\"\nWordsInMinorFrame = 50\n"},
        {"legacy-too-small", "[Frame]\nFrameSync = \"FE6B2840\"\nWordsInMinorFrame = 1\n"},
        {"bits-too-small", "[Frame]\nBitsPerFrame = 3\nFrameSync = \"AB\"\n"},
        {"bits-and-legacy", "[Frame]\nBitsPerFrame = 800\nWordsInMinorFrame = 50\n"},
        {"no-randomized", "[Frame]\nBitsPerFrame = 800\nFrameSync = \"FE6B2840\"\nInverted = true\n"},
        {"randomized", "[Frame]\nBitsPerFrame = 800\nFrameSync = \"FE6B2840\"\nFrameSyncMask = \"FFFF0000\"\nRandomized = true\nInverted = false\n"},
        {"empty-values", "[Frame]\nFrameSync = \"\"\nFrameSyncMask = \"\"\n"},
        {"bom", std::string("\xEF\xBB\xBF[Frame]\nBitsPerFrame = 8000\nFrameSync = \"334AABBF\"\n")},
        {"non-ascii", std::string("[Frame]\nFrameSync = \"\xC3\xA9\xC3\xA8\"\nBitsPerFrame = 800\n")},
        {"empty-file", std::string()},
        {"junk", "this is {{ not toml\n"},
    };
    for (const auto& [name, bytes] : odd)
        paths.push_back(writeFile(dir.filePath(std::string(name) + ".toml"), bytes));
    paths.push_back(dir.filePath("does-not-exist.toml"));

    for (const std::string& path : paths)
    {
        REQUIRE_MESSAGE(!path.empty(), "a fixture could not be written");
        const std::string utf8 = path;
        const FrameSyncFile want = readFrameSyncFile(textfile::pathFromUtf8(path));
        tmdq_frame_sync_file* file = nullptr;
        REQUIRE(tmdq_frame_sync_file_read(utf8.c_str(), &file) == int(TMDQ_OK));
        REQUIRE(file != nullptr);
        CHECK(describeSyncFile(file) == describeSyncFile(want));
        tmdq_frame_sync_file_free(file);
    }

    // The shipped PRN15 file is not "nothing": the comparison above is not vacuous.
    const FrameSyncFile prn = readFrameSyncFile(textfile::pathFromUtf8(paths[1]));
    CHECK(prn.pattern == std::string("334AABBF"));
    REQUIRE(prn.bitsPerFrame.has_value());
    REQUIRE(prn.randomized.has_value());

    // Arguments.
    tmdq_frame_sync_file* none = reinterpret_cast<tmdq_frame_sync_file*>(std::uintptr_t{1});
    CHECK(tmdq_frame_sync_file_read(nullptr, &none) == int(TMDQ_ERR_BADARG));
    REQUIRE(none == nullptr);
    CHECK(tmdq_frame_sync_file_read("x", nullptr) == int(TMDQ_ERR_BADARG));
    tmdq_frame_sync_file_info info{};
    info.struct_size = sizeof(info);
    CHECK(tmdq_frame_sync_file_info_get(nullptr, &info) == int(TMDQ_ERR_BADARG));
    const std::string good = paths[1];
    tmdq_frame_sync_file* file = nullptr;
    REQUIRE(tmdq_frame_sync_file_read(good.c_str(), &file) == int(TMDQ_OK));
    CHECK(tmdq_frame_sync_file_info_get(file, nullptr) == int(TMDQ_ERR_BADARG));
    info.struct_size = sizeof(info) - 1;
    CHECK(tmdq_frame_sync_file_info_get(file, &info) == int(TMDQ_ERR_BADARG));
    tmdq_frame_sync_file_free(file);
    tmdq_frame_sync_file_free(nullptr);
}

TEST_CASE("Abi: a frame sync file is written like the core")
{
    Scratch dir;
    struct Args { const char* pattern; const char* mask; int bits; int inverted; int randomized; };
    const Args cases[] = {
        {"FE6B2840", "", 800, 0, 0},
        {"334AABBF", "FFFFFFFF", 32767, 0, 1},
        {"A345CA5C", "FFFF0000", 2047, 1, 0},
        {"\xC3\xA9", "", 8000, 1, 1},
        {"", "", 0, 0, 0},
    };
    int n = 0;
    for (const Args& a : cases)
    {
        const std::string viaAbi = dir.filePath(std::format("abi{}.toml", n));
        const std::string viaCore = dir.filePath(std::format("core{}.toml", n));
        ++n;
        const std::string utf8 = viaAbi;
        REQUIRE(tmdq_frame_sync_file_write(utf8.c_str(), a.pattern, a.mask, a.bits, a.inverted, a.randomized) == int(TMDQ_OK));
        REQUIRE(writeFrameSyncFile(textfile::pathFromUtf8(viaCore), a.pattern, a.mask, a.bits, a.inverted != 0, a.randomized != 0));
        CHECK(slurp(viaAbi) == slurp(viaCore));

        // And it reads back as what was written.
        tmdq_frame_sync_file* file = nullptr;
        REQUIRE(tmdq_frame_sync_file_read(utf8.c_str(), &file) == int(TMDQ_OK));
        CHECK(describeSyncFile(file) == describeSyncFile(readFrameSyncFile(textfile::pathFromUtf8(viaCore))));
        tmdq_frame_sync_file_free(file);
    }

    // NULL text reads as empty; a path that cannot be written is an I/O error, not a crash.
    const std::string viaNull = dir.filePath("null.toml");
    const std::string nullUtf8 = viaNull;
    REQUIRE(tmdq_frame_sync_file_write(nullUtf8.c_str(), nullptr, nullptr, 800, 0, 0) == int(TMDQ_OK));
    const std::string viaEmpty = dir.filePath("empty.toml");
    REQUIRE(writeFrameSyncFile(textfile::pathFromUtf8(viaEmpty), "", "", 800, false, false));
    CHECK(slurp(viaNull) == slurp(viaEmpty));

    const std::string dirPath = dir.path();   // a directory is not a file
    CHECK(tmdq_frame_sync_file_write(dirPath.c_str(), "AB", "", 800, 0, 0) == int(TMDQ_ERR_IO));
    REQUIRE(!std::string(tmdq_last_error()).empty());
    CHECK(tmdq_frame_sync_file_write(nullptr, "AB", "", 800, 0, 0) == int(TMDQ_ERR_BADARG));
}

TEST_CASE("Abi: template matching agrees with the core")
{
    auto run = [](const std::vector<int>& t, const std::vector<int>& f) {
        tmdq_match* match = nullptr;
        const int32_t status = tmdq_template_match(t.empty() ? nullptr : t.data(), int(t.size()),
                                                   f.empty() ? nullptr : f.data(), int(f.size()), &match);
        return std::pair<int32_t, tmdq_match*>{status, match};
    };
    auto same = [](tmdq_match* m, const TemplateMatcher::MatchResult& want) {
        tmdq_match_info info{};
        info.struct_size = sizeof(info);
        if (tmdq_match_info_get(m, &info) != TMDQ_OK) return false;
        if ((info.ok != 0) != want.ok || std::size_t(info.missing_count) != want.missing.size() ||
            std::size_t(info.extra_count) != want.extra.size())
            return false;
        for (int32_t i = 0; i < info.missing_count; ++i) if (info.missing[i] != want.missing[std::size_t(i)]) return false;
        for (int32_t i = 0; i < info.extra_count; ++i) if (info.extra[i] != want.extra[std::size_t(i)]) return false;
        return true;
    };

    // Fixed cases first: equal sets, order and repeats, missing, extra, both, empty.
    const std::vector<std::pair<std::vector<int>, std::vector<int>>> fixed = {
        {{13, 14, 15}, {15, 14, 13}}, {{13, 13, 14}, {14, 13}}, {{13, 14, 15}, {13, 14}}, {{13}, {13, 14, 15}},
        {{1, 2, 3}, {3, 4, 5}}, {{}, {}}, {{}, {7}}, {{7}, {}},
    };
    for (const auto& [t, f] : fixed)
    {
        auto [status, match] = run(t, f);
        REQUIRE(status == int(TMDQ_OK));
        REQUIRE(same(match, TemplateMatcher::match(t, f)));
        tmdq_match_free(match);
    }
    // The comparison is not vacuous: one of those really is a mismatch, another really matches.
    REQUIRE(!TemplateMatcher::match({1, 2, 3}, {3, 4, 5}).ok);
    REQUIRE(TemplateMatcher::match({13, 13, 14}, {14, 13}).ok);

    // Random sets, with repeats.
    std::mt19937 rng(20261023);
    int matched = 0, mismatched = 0;
    for (int i = 0; i < 2000; ++i)
    {
        std::vector<int> t, f;
        const int nt = int(rng() % 6), nf = int(rng() % 6);
        for (int k = 0; k < nt; ++k) t.push_back(int(rng() % 8));
        for (int k = 0; k < nf; ++k) f.push_back(int(rng() % 8));
        auto [status, match] = run(t, f);
        REQUIRE(status == int(TMDQ_OK));
        const auto want = TemplateMatcher::match(t, f);
        REQUIRE_MESSAGE(same(match, want), std::format("case {}", i));
        (want.ok ? matched : mismatched)++;
        tmdq_match_free(match);
    }
    REQUIRE_MESSAGE((matched > 20 && mismatched > 500), std::format("{} matched, {} mismatched", matched, mismatched));

    // Arguments.
    tmdq_match* none = reinterpret_cast<tmdq_match*>(std::uintptr_t{1});
    const int32_t ids[] = {1, 2};
    CHECK(tmdq_template_match(ids, -1, ids, 2, &none) == int(TMDQ_ERR_BADARG));
    REQUIRE(none == nullptr);
    CHECK(tmdq_template_match(ids, 2, ids, -1, &none) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_template_match(nullptr, 2, ids, 2, &none) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_template_match(ids, 2, nullptr, 2, &none) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_template_match(ids, 2, ids, 2, nullptr) == int(TMDQ_ERR_BADARG));
    REQUIRE(tmdq_template_match(nullptr, 0, nullptr, 0, &none) == int(TMDQ_OK));   // two empty sets
    tmdq_match_info info{};
    info.struct_size = sizeof(info);
    REQUIRE(tmdq_match_info_get(none, &info) == int(TMDQ_OK));
    CHECK(info.ok == 1);
    CHECK(tmdq_match_info_get(none, nullptr) == int(TMDQ_ERR_BADARG));
    info.struct_size = sizeof(info) - 1;
    CHECK(tmdq_match_info_get(none, &info) == int(TMDQ_ERR_BADARG));
    tmdq_match_free(none);
    CHECK(tmdq_match_info_get(nullptr, &info) == int(TMDQ_ERR_BADARG));
    tmdq_match_free(nullptr);
}

TEST_CASE("Abi: a CSV is parsed like the core")
{
    // Two real exports of v2.x, trimmed to their header and a few rows (tests/fixtures): a wide one of one stream's
    // receiver channels, and a sparse one of four streams' lock and missed frames, one row to a stream. Committed, so the
    // case covers the same files on every machine; the files written below are what the rest of the comparison stands on.
    std::vector<std::string> files;
    for (const char* name : { "csv_export_snr_plot.csv", "csv_export_four_streams.csv" })
    {
        const std::string path = rootUtf8() + "/tests/fixtures/" + name;
        REQUIRE_MESSAGE(fs::exists(textfile::pathFromUtf8(path)), path);
        files.push_back(path);
    }

    // One written by the writer too, with every metric and a second source.
    Scratch dir;
    CsvColumns cols;
    cols.add(TMDQ_METRIC_FRAME_SYNC_LOCK, "Ch 13", 0, {0.0, 1.0, 2.5}, {99.5, 100.0, 12.25});
    cols.add(TMDQ_METRIC_ACCUMULATED_MISSED_FRAMES, "Ch 13", 0, {0.0, 1.0, 2.5}, {0.0, 3.0, 7.0});
    cols.add(TMDQ_METRIC_SNR, "13 - Prn L_RCVR2", 2, {0.0, 1.0, 2.5}, {-3.5, 10.0, 22.125});
    cols.finish();
    const std::string generated = dir.filePath("generated.csv");
    REQUIRE(CsvSeriesWriter::write(textfile::pathFromUtf8(generated), cols.core, CsvTimeBase{120, 77125.5}));
    files.push_back(generated);

    // One with a malformed row in it, which the parser skips and counts.
    const std::string skipping = writeFile(dir.filePath("skipping.csv"),
        "Time (DOY:HH:MM:SS.mmm),A Lock (%),B Accumulated Missed Frames\n"
        "120:00:00:00.000,5,1\n"
        "not-a-time,6,2\n"
        "120:00:00:01.000\n"
        "120:00:00:02.000,7,3\n");
    files.push_back(skipping);

    bool sawSkipped = false, sawStreamOrder = false;
    for (const std::string& path : files)
    {
        const std::string utf8 = path;
        const CsvParseResult want = CsvSeriesParser::parse(textfile::pathFromUtf8(path));
        REQUIRE_MESSAGE(want.success, path);
        REQUIRE(want.series.size() >= 2);
        sawSkipped = sawSkipped || want.skippedRows > 0;
        for (const CsvSeries& series : want.series)
            sawStreamOrder = sawStreamOrder || series.streamOrder == 13;
        tmdq_csv* csv = nullptr;
        REQUIRE(tmdq_csv_parse(utf8.c_str(), &csv) == int(TMDQ_OK));
        REQUIRE(csv != nullptr);
        const std::string diff = csvDifferences(csv, want);
        REQUIRE_MESSAGE(diff.empty(), (path + ": " + diff));
        tmdq_csv_free(csv);
    }
    REQUIRE_MESSAGE((sawSkipped && sawStreamOrder), "the files should include a skipped row and a stream order, or the comparison proves little");

    // Files the parser refuses: not there, junk, the wrong header, a header and no data.
    const std::string junk = writeFile(dir.filePath("junk.csv"), "this is not a series file\n1,2,3\n");
    const std::string wrongHeader = writeFile(dir.filePath("wrong.csv"), "Day,Time,A Lock (%)\n1,00:00:00,5\n");
    const std::string noData = writeFile(dir.filePath("nodata.csv"), "Time (DOY:HH:MM:SS.mmm),A Lock (%)\n");
    for (const std::string& path : { tempUtf8() + "/tst_abi_no_such.csv", junk, wrongHeader, noData })
    {
        REQUIRE(!CsvSeriesParser::parse(textfile::pathFromUtf8(path)).success);
        const std::string utf8 = path;
        tmdq_csv* csv = reinterpret_cast<tmdq_csv*>(std::uintptr_t{1});
        CHECK(tmdq_csv_parse(utf8.c_str(), &csv) == int(TMDQ_ERR_PREPARE));
        REQUIRE(csv == nullptr);
        REQUIRE(!std::string(tmdq_last_error()).empty());
    }

    // Arguments and indices.
    tmdq_csv* csv = nullptr;
    CHECK(tmdq_csv_parse(nullptr, &csv) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_csv_parse("x", nullptr) == int(TMDQ_ERR_BADARG));
    const std::string good = generated;
    REQUIRE(tmdq_csv_parse(good.c_str(), &csv) == int(TMDQ_OK));
    tmdq_csv_info info{};
    info.struct_size = sizeof(info);
    REQUIRE(tmdq_csv_info_get(csv, &info) == int(TMDQ_OK));
    tmdq_csv_series_info series{};
    series.struct_size = sizeof(series);
    REQUIRE(tmdq_csv_series_get(csv, info.series_count, &series) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_csv_series_get(csv, -1, &series) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_csv_series_get(csv, 0, nullptr) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_csv_info_get(csv, nullptr) == int(TMDQ_ERR_BADARG));
    tmdq_csv_info shortInfo{};
    shortInfo.struct_size = sizeof(shortInfo) - 1;
    CHECK(tmdq_csv_info_get(csv, &shortInfo) == int(TMDQ_ERR_BADARG));
    tmdq_csv_series_info shortSeries{};
    shortSeries.struct_size = sizeof(shortSeries) - 1;
    CHECK(tmdq_csv_series_get(csv, 0, &shortSeries) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_csv_info_get(nullptr, &info) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_csv_series_get(nullptr, 0, &series) == int(TMDQ_ERR_BADARG));
    tmdq_csv_free(csv);
    tmdq_csv_free(nullptr);
}

TEST_CASE("Abi: a CSV is written like the core")
{
    Scratch dir;

    // Columns that exercise the writer: every metric, two sources, a name with non-ASCII text (a comma cannot be written: the format has no quoting),
    // times that fall in the same millisecond, a column with more times than values, and an empty one.
    CsvColumns cols;
    cols.add(TMDQ_METRIC_FRAME_SYNC_LOCK, "Ch 13", 0, {0.0, 1.0, 2.0, 2.0004}, {99.5, 100.0, 12.25, 13.5});
    cols.add(TMDQ_METRIC_ACCUMULATED_MISSED_FRAMES, "Ch 13", 0, {0.0, 1.0, 2.0}, {0.0, 3.0, 7.0});
    cols.add(TMDQ_METRIC_SNR, "Ch 13 L_RCVR2", 0, {0.0, 1.5}, {-3.5, 10.0});
    cols.add(TMDQ_METRIC_SNR, "S\xC3\xA9rie 2 L_RCVR1", 2, {0.5, 1.5, 2.5}, {1.0, 2.0, 3.0});
    cols.add(TMDQ_METRIC_FRAME_SYNC_LOCK, "uneven", 3, {0.0, 1.0, 2.0}, {5.0, 6.0});
    cols.add(TMDQ_METRIC_FRAME_SYNC_LOCK, "empty", 3, {}, {});
    cols.finish();

    const std::string viaAbi = dir.filePath("abi.csv");
    const std::string viaCore = dir.filePath("core.csv");
    const std::string utf8 = viaAbi;
    REQUIRE(tmdq_csv_write(utf8.c_str(), cols.pointers.data(), int(cols.pointers.size()), 120, 77125.5) == int(TMDQ_OK));
    REQUIRE(CsvSeriesWriter::write(textfile::pathFromUtf8(viaCore), cols.core, CsvTimeBase{120, 77125.5}));
    CHECK(slurp(viaAbi) == slurp(viaCore));
    REQUIRE(slurp(viaAbi).size() > 200);

    // The time base reaches the stamps.
    const std::string viaOtherBase = dir.filePath("other.csv");
    const std::string otherUtf8 = viaOtherBase;
    REQUIRE(tmdq_csv_write(otherUtf8.c_str(), cols.pointers.data(), int(cols.pointers.size()), 7, 100.25) == int(TMDQ_OK));
    REQUIRE(CsvSeriesWriter::write(textfile::pathFromUtf8(dir.filePath("other-core.csv")), cols.core, CsvTimeBase{7, 100.25}));
    CHECK(slurp(viaOtherBase) == slurp(dir.filePath("other-core.csv")));
    REQUIRE(slurp(viaOtherBase) != slurp(viaAbi));

    // And what the ABI wrote parses back, through the ABI, as the core parses it.
    tmdq_csv* csv = nullptr;
    REQUIRE(tmdq_csv_parse(utf8.c_str(), &csv) == int(TMDQ_OK));
    const std::string diff = csvDifferences(csv, CsvSeriesParser::parse(textfile::pathFromUtf8(viaAbi)));
    REQUIRE_MESSAGE(diff.empty(), diff);
    tmdq_csv_free(csv);

    // Refusals.
    CHECK(tmdq_csv_write(nullptr, cols.pointers.data(), 1, 0, 0.0) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_csv_write(utf8.c_str(), cols.pointers.data(), 0, 0, 0.0) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_csv_write(utf8.c_str(), cols.pointers.data(), -1, 0, 0.0) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_csv_write(utf8.c_str(), nullptr, 1, 0, 0.0) == int(TMDQ_ERR_BADARG));
    const tmdq_csv_column* withNull[] = { cols.pointers[0], nullptr };
    CHECK(tmdq_csv_write(utf8.c_str(), withNull, 2, 0, 0.0) == int(TMDQ_ERR_BADARG));
    REQUIRE(std::string(tmdq_last_error()).find("column 1") != std::string::npos);
    tmdq_csv_column shortColumn = cols.wire[0];
    shortColumn.struct_size = sizeof(shortColumn) - 1;
    const tmdq_csv_column* withShort[] = { &shortColumn };
    CHECK(tmdq_csv_write(utf8.c_str(), withShort, 1, 0, 0.0) == int(TMDQ_ERR_BADARG));
    tmdq_csv_column badMetric = cols.wire[0];
    badMetric.metric_type = 9;
    const tmdq_csv_column* withBadMetric[] = { &badMetric };
    CHECK(tmdq_csv_write(utf8.c_str(), withBadMetric, 1, 0, 0.0) == int(TMDQ_ERR_BADARG));
    REQUIRE(std::string(tmdq_last_error()).find("metric") != std::string::npos);
    tmdq_csv_column negativeCount = cols.wire[0];
    negativeCount.x_count = -1;
    const tmdq_csv_column* withNegative[] = { &negativeCount };
    CHECK(tmdq_csv_write(utf8.c_str(), withNegative, 1, 0, 0.0) == int(TMDQ_ERR_BADARG));
    tmdq_csv_column noArray = cols.wire[0];
    noArray.y_values = nullptr;
    const tmdq_csv_column* withNoArray[] = { &noArray };
    CHECK(tmdq_csv_write(utf8.c_str(), withNoArray, 1, 0, 0.0) == int(TMDQ_ERR_BADARG));
    tmdq_csv_column noTimes = cols.wire[0];
    REQUIRE(noTimes.x_count > 0);
    noTimes.x_values = nullptr;
    const tmdq_csv_column* withNoTimes[] = { &noTimes };
    CHECK(tmdq_csv_write(utf8.c_str(), withNoTimes, 1, 0, 0.0) == int(TMDQ_ERR_BADARG));
    REQUIRE(std::string(tmdq_last_error()).find("does not match its array") != std::string::npos);
    tmdq_csv_column noName = cols.wire[0];
    noName.name_utf8 = nullptr;
    const tmdq_csv_column* withNoName[] = { &noName };
    REQUIRE(tmdq_csv_write(dir.filePath("noname.csv").c_str(), withNoName, 1, 0, 0.0) == int(TMDQ_OK));   // a NULL name reads as empty

    // A path that cannot be created is an I/O error.
    const std::string dirPath = dir.path();
    CHECK(tmdq_csv_write(dirPath.c_str(), cols.pointers.data(), 1, 0, 0.0) == int(TMDQ_ERR_IO));
    REQUIRE(!std::string(tmdq_last_error()).empty());
}

TEST_CASE("Abi: clock stamps agree with the core")
{
    std::mt19937 rng(20261023);
    int checked = 0;
    for (int i = 0; i < 3000; ++i)
    {
        const int day = int(rng() % 400) - 5;
        const double base = double(rng() % 86400) + double(rng() % 1000) / 1000.0;
        const double elapsed = (double(int(rng() % 400000)) - 20000.0) / 7.0;
        const bool ms = rng() % 2;
        const std::string want = clocktime::format(day, base, elapsed, ms);

        char exact[64] = {};
        REQUIRE(tmdq_clock_time_format(day, base, elapsed, ms ? 1 : 0, exact, sizeof(exact)) == int(want.size()));
        CHECK(std::string(exact) == want);
        REQUIRE(tmdq_clock_time_format(day, base, elapsed, ms ? 1 : 0, nullptr, 0) == int(want.size()));
        ++checked;
    }
    CHECK(checked == 3000);

    // A known stamp, so the oracle is not the only witness: 1 day, 1 h, 1 min, 1.5 s after 120:00:00:00.
    char buf[64] = {};
    REQUIRE(tmdq_clock_time_format(120, 0.0, 86400.0 + 3600.0 + 60.0 + 1.5, 1, buf, sizeof(buf)) > 0);
    CHECK(std::string(buf) == std::string("121:01:01:01.500"));
    REQUIRE(tmdq_clock_time_format(120, 0.0, 86400.0 + 3600.0 + 60.0 + 1.5, 0, buf, sizeof(buf)) > 0);
    CHECK(std::string(buf) == std::string("121:01:01:01"));

    // Truncation and refusals.
    char tight[6];
    std::memset(tight, 'x', sizeof(tight));
    CHECK(tmdq_clock_time_format(120, 0.0, 0.0, 1, tight, sizeof(tight)) == 16);
    CHECK(std::string(tight) == std::string("120:0"));
    CHECK(int(tight[5]) == 0);
    CHECK(tmdq_clock_time_format(120, 0.0, 0.0, 1, nullptr, 10) == -1);
    CHECK(tmdq_clock_time_format(120, 0.0, 0.0, 1, buf, -1) == -1);
}


// --- the receiver-parameters file (revision 1, plan step 4.1a) ------------------------------------------------------

namespace {

tmdq_receiver_params abiParams(int polarity, int slope, double scale, int receivers, int channels)
{
    tmdq_receiver_params p{};
    p.struct_size = sizeof(p);
    p.polarity_index = polarity;
    p.slope_index = slope;
    p.scale_db_per_v = scale;
    p.num_receivers = receivers;
    p.receiver_channels = channels;
    return p;
}

ReceiverParams coreParams(const tmdq_receiver_params& p)
{
    ReceiverParams r;
    r.polarityIndex = p.polarity_index;
    r.slopeIndex = p.slope_index;
    r.scaleDdBPerV = p.scale_db_per_v;
    r.numReceivers = p.num_receivers;
    r.receiverChannels = p.receiver_channels;
    return r;
}

/// Both kinds as one line of text, so a difference shows every field at once.
std::string describe(const ReceiverParams& r)
{
    return std::format("polarity={} slope={} scale={:.17g} receivers={} channels={}", r.polarityIndex, r.slopeIndex,
                       r.scaleDdBPerV, r.numReceivers, r.receiverChannels);
}

std::string describe(const tmdq_receiver_params& p) { return describe(coreParams(p)); }

} // namespace

TEST_CASE("Abi: a receiver parameters file is read like the core")
{
    // Every read rule, each in a file of its own, against the core called directly, from two starting points (the
    // application's defaults, and values no file below gives, so a field read from the wrong key shows).
    Scratch s;
    const std::vector<std::pair<std::string, std::string>> files = {
        {"full", "[Parameters]\r\nPolarity=1\r\nSlope=2\r\nScale=17.5\r\n[Receivers]\r\nCount=12\r\nChannelsPerReceiver=4\r\n"},
        {"scale-only", "[Parameters]\r\nScale=33\r\n"},
        {"legacy-counts", "[Parameters]\r\nNumReceivers=9\r\nReceiverChannels=2\r\n"},
        {"both-count-keys", "[Parameters]\r\nNumReceivers=9\r\nReceiverChannels=2\r\n[Receivers]\r\nCount=5\r\nChannelsPerReceiver=6\r\n"},
        {"receivers-without-channels", "[Receivers]\r\nCount=7\r\n[Parameters]\r\nReceiverChannels=8\r\n"},
        {"not-positive", "[Parameters]\r\nScale=0\r\n[Receivers]\r\nCount=-3\r\nChannelsPerReceiver=0\r\n"},
        {"negative-scale", "[Parameters]\r\nScale=-4.5\r\n"},
        {"out-of-range-indices", "[Parameters]\r\nPolarity=7\r\nSlope=-1\r\n"},
        {"quoted-scale", "[Parameters]\r\nScale=\"20\"\r\n"},
        {"word-map-only", "[L_RCVR1]\r\nWord=3\r\n"},
        {"empty", ""},
    };
    std::vector<std::string> paths;
    for (const auto& [name, text] : files)
        paths.push_back(writeFile(s.filePath(name + ".toml"), text));
    paths.push_back(s.filePath("missing.toml"));
    for (const char* shipped : {"default.toml", "RASA.toml", "TRC.toml"})
        paths.push_back(utf8Of(repoFile(fs::path("settings") / "receiver_params" / shipped)));

    const tmdq_receiver_params starts[] = {abiParams(0, 3, 20.0, 16, 3), abiParams(1, 1, 99.25, 41, 43)};
    int changed = 0;
    for (const std::string& path : paths)
    {
        for (const tmdq_receiver_params& start : starts)
        {
            INFO(path);
            tmdq_receiver_params out{};
            out.struct_size = sizeof(out);
            REQUIRE(tmdq_receiver_params_read(path.c_str(), &start, &out) == int(TMDQ_OK));
            const ReceiverParams want = readReceiverParams(textfile::pathFromUtf8(path), coreParams(start));
            CHECK(describe(out) == describe(want));
            changed += describe(out) != describe(start);
        }
    }
    // The corpus reaches both outcomes: some files change what was shown, some change nothing.
    CHECK(changed > 10);

    // Hand-checked, so the comparison is not the only witness: RASA is 12 receivers of 3 channels.
    tmdq_receiver_params rasa{};
    rasa.struct_size = sizeof(rasa);
    REQUIRE(tmdq_receiver_params_read(utf8Of(repoFile("settings/receiver_params/RASA.toml")).c_str(), nullptr, &rasa) == int(TMDQ_OK));
    CHECK(rasa.num_receivers == 12);
    CHECK(rasa.receiver_channels == 3);

    // NULL defaults are the application's own.
    tmdq_receiver_params none{};
    none.struct_size = sizeof(none);
    REQUIRE(tmdq_receiver_params_read(s.filePath("missing.toml").c_str(), nullptr, &none) == int(TMDQ_OK));
    CHECK(describe(none) == describe(ReceiverParams()));
}

TEST_CASE("Abi: a receiver parameters file is saved like the core")
{
    // Each case saved through the ABI and through saveReceiverParamsFile: the same verdict, the same
    // error text and the same bytes. Then what was saved reads back as the scalars that were saved.
    Scratch s;
    const std::string rasa = utf8Of(repoFile("settings/receiver_params/RASA.toml"));
    const std::string bad = writeFile(s.filePath("not-a-map.toml"), "[Parameters]\r\nScale=3\r\n");
    struct Case { const char* name; tmdq_receiver_params params; std::string source; bool nullSource; int words; };
    const Case cases[] = {
        {"default-map", abiParams(0, 3, 20.0, 16, 3), "", false, 50},
        {"null-source", abiParams(1, 2, 12.5, 4, 2), "", true, 20},
        {"rasa-map", abiParams(1, 0, 7.25, 12, 3), rasa, false, 50},
        {"missing-source", abiParams(0, 1, 30.0, 2, 3), s.filePath("nowhere.toml"), false, 20},
        {"source-without-a-map", abiParams(0, 1, 30.0, 2, 3), bad, false, 20},
        {"rasa-map-too-big-for-the-frame", abiParams(0, 3, 20.0, 2, 2), rasa, false, 10},
        {"default-map-too-big-for-the-frame", abiParams(0, 3, 20.0, 16, 3), "", false, 10},
        {"no-receivers", abiParams(0, 3, 20.0, 0, 3), "", false, 50},
    };
    int saved = 0, refused = 0;
    for (const Case& c : cases)
    {
        INFO(c.name);
        const std::string viaAbi = s.filePath(std::string(c.name) + "-abi.toml");
        const std::string viaCore = s.filePath(std::string(c.name) + "-core.toml");
        const int32_t status = tmdq_receiver_params_save(viaAbi.c_str(), &c.params, c.nullSource ? nullptr : c.source.c_str(), c.words);
        const std::string abiError = tmdq_last_error();
        std::string coreError;
        const bool coreOk = saveReceiverParamsFile(textfile::pathFromUtf8(viaCore), coreParams(c.params),
                                                               textfile::pathFromUtf8(c.source), c.words, coreError);
        CHECK((status == int(TMDQ_OK)) == coreOk);
        CHECK(slurp(viaAbi) == slurp(viaCore));
        if (coreOk)
        {
            ++saved;
            CHECK(abiError.empty());
            tmdq_receiver_params back{};
            back.struct_size = sizeof(back);
            REQUIRE(tmdq_receiver_params_read(viaAbi.c_str(), nullptr, &back) == int(TMDQ_OK));
            CHECK(describe(back) == describe(c.params));
        }
        else
        {
            ++refused;
            CHECK(status == int(TMDQ_ERR_IO));
            CHECK(abiError == coreError);
            CHECK(!abiError.empty());
        }
    }
    CHECK(saved >= 4);
    CHECK(refused >= 2);

    // The frame's word count reaches the core as given: swept across the edge where the default 16 x 3 map and RASA's
    // stop fitting, every count gives the core's verdict, and both sides of each edge are reached.
    for (const std::string& source : {std::string(), rasa})
    {
        bool fits = false, tooBig = false;
        for (int words = 40; words <= 60; ++words)
        {
            INFO("source " << (source.empty() ? "default" : "RASA") << ", words " << words);
            const tmdq_receiver_params p = abiParams(0, 3, 20.0, 16, 3);
            const std::string viaAbi = s.filePath("edge-abi.toml");
            const std::string viaCore = s.filePath("edge-core.toml");
            std::error_code ignored;
            fs::remove(textfile::pathFromUtf8(viaAbi), ignored);
            fs::remove(textfile::pathFromUtf8(viaCore), ignored);
            const bool abiOk = tmdq_receiver_params_save(viaAbi.c_str(), &p, source.c_str(), words) == int(TMDQ_OK);
            std::string coreError;
            const bool coreOk = saveReceiverParamsFile(textfile::pathFromUtf8(viaCore), coreParams(p),
                                                                   textfile::pathFromUtf8(source), words, coreError);
            CHECK(abiOk == coreOk);
            CHECK(slurp(viaAbi) == slurp(viaCore));
            (coreOk ? fits : tooBig) = true;
        }
        CHECK(fits);
        CHECK(tooBig);
    }

    // The map is the source's when it loads: RASA's word map, not the default one, is what the file now carries.
    FrameSetup back;
    REQUIRE(back.tryLoadingFile(textfile::pathFromUtf8(s.filePath("rasa-map-abi.toml")), 50));
    FrameSetup original;
    REQUIRE(original.tryLoadingFile(textfile::pathFromUtf8(rasa), 50));
    REQUIRE(back.length() == original.length());
    for (int i = 0; i < back.length(); ++i)
    {
        CHECK(back.getParameter(i)->name == original.getParameter(i)->name);
        CHECK(back.getParameter(i)->word == original.getParameter(i)->word);
    }

    // A file that cannot be written (a folder of that name is in the way) says so in the core's words.
    fs::create_directories(textfile::pathFromUtf8(s.filePath("blocked.toml")));
    const tmdq_receiver_params p = abiParams(0, 3, 20.0, 2, 3);
    CHECK(tmdq_receiver_params_save(s.filePath("blocked.toml").c_str(), &p, nullptr, 20) == int(TMDQ_ERR_IO));
    CHECK(std::string(tmdq_last_error()) == "Could not write 'blocked.toml'.");
}

TEST_CASE("Abi: the words in a minor frame are the core's")
{
    for (int bits : {-17, -1, 0, 1, 15, 16, 17, 31, 32, 33, 64, 790, 800, 801, 2047, 32767, 32768, 65536})
    {
        INFO(bits);
        CHECK(tmdq_words_in_minor_frame(bits) == FrameSetup::wordsInMinorFrame(bits));
    }
    CHECK(tmdq_words_in_minor_frame(800) == 50);
    CHECK(tmdq_words_in_minor_frame(801) == 51);
}

TEST_CASE("Abi: bad arguments to the receiver parameter functions are refused")
{
    Scratch s;
    const std::string path = writeFile(s.filePath("p.toml"), "[Parameters]\r\nScale=5\r\n");
    tmdq_receiver_params out{};
    out.struct_size = sizeof(out);
    const tmdq_receiver_params good = abiParams(0, 3, 20.0, 2, 3);

    CHECK(tmdq_receiver_params_read(nullptr, &good, &out) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_receiver_params_read(path.c_str(), &good, nullptr) == int(TMDQ_ERR_BADARG));
    tmdq_receiver_params small = out;
    small.struct_size = sizeof(small) - 1;
    CHECK(tmdq_receiver_params_read(path.c_str(), &good, &small) == int(TMDQ_ERR_BADARG));
    tmdq_receiver_params smallDefaults = good;
    smallDefaults.struct_size = sizeof(smallDefaults) - 1;
    CHECK(tmdq_receiver_params_read(path.c_str(), &smallDefaults, &out) == int(TMDQ_ERR_BADARG));
    REQUIRE(std::string(tmdq_last_error()).find("struct_size") != std::string::npos);

    CHECK(tmdq_receiver_params_save(nullptr, &good, nullptr, 20) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_receiver_params_save(s.filePath("q.toml").c_str(), nullptr, nullptr, 20) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_receiver_params_save(s.filePath("q.toml").c_str(), &smallDefaults, nullptr, 20) == int(TMDQ_ERR_BADARG));
    CHECK_FALSE(fs::exists(textfile::pathFromUtf8(s.filePath("q.toml"))));

    // A struct from a newer host (bigger) is read no further than this library knows, and its output written no
    // further than the host's own size allows.
    struct Bigger { tmdq_receiver_params p; int32_t extra[4]; };
    Bigger defaults{};
    defaults.p = good;
    defaults.p.struct_size = sizeof(Bigger);
    defaults.extra[0] = 0x5A5A5A5A;
    Bigger result{};
    result.p.struct_size = sizeof(Bigger);
    result.extra[0] = 0x12345678;
    REQUIRE(tmdq_receiver_params_read(path.c_str(), &defaults.p, &result.p) == int(TMDQ_OK));
    CHECK(result.p.scale_db_per_v == 5.0);
    CHECK(result.p.num_receivers == 2);
    CHECK(result.extra[0] == 0x12345678);

    // A successful call clears an earlier error.
    CHECK(tmdq_receiver_params_read(nullptr, nullptr, nullptr) == int(TMDQ_ERR_BADARG));
    REQUIRE(tmdq_receiver_params_read(path.c_str(), nullptr, &out) == int(TMDQ_OK));
    CHECK(std::string(tmdq_last_error()).empty());
}
