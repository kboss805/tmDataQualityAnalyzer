// A phased array antenna's SNR log (US10.0), read by PhasedArrayLogParser: the layout the third party writes (an ISO 8601 UTC
// time, a unit name, a polarization, one column for each stream), the three series each stream becomes, and what is
// rejected or skipped. The committed sample is tests/data/test files/av_snr_log.csv; the rest are small files written here.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "csvseriesparser.h"
#include "doctest.h"
#include "phasedarraylogparser.h"
#include "test_support.h"

using namespace tmdq_test;

namespace {

const std::string kHeader = "Time, Name, Polarity, Stream 1, Stream 2";

/// The log's text with CR LF line ends, as the third party's file has them.
std::string logText(const std::vector<std::string>& lines)
{
    std::string text;
    for (const std::string& l : lines)
    {
        text += l + "\r\n";
    }
    return text;
}

CsvParseResult parseText(const TempDir& dir, const std::string& text)
{
    const fs::path file = dir.file("log.csv");
    writeBytes(file, text);
    return PhasedArrayLogParser::parse(file);
}

}  // namespace

TEST_CASE("PhasedArrayLog: an ISO 8601 UTC time is a day, a day of the year and the seconds into the day")
{
    long long days = 0;
    int day_of_year = 0;
    double seconds = 0.0;

    REQUIRE(PhasedArrayLogParser::parseIsoUtc("1970-01-01T00:00:00Z", days, day_of_year, seconds));
    CHECK(days == 0);
    CHECK(day_of_year == 1);
    CHECK(seconds == 0.0);

    REQUIRE(PhasedArrayLogParser::parseIsoUtc("2026-11-13T19:15:35.513Z", days, day_of_year, seconds));
    CHECK(day_of_year == 317);   // 2026 is not a leap year: 304 days to the end of October, plus 13
    CHECK(seconds == doctest::Approx(19 * 3600 + 15 * 60 + 35.513));

    // 2024 is a leap year, so its last day is day 366; 2100 is not one, 2000 is.
    REQUIRE(PhasedArrayLogParser::parseIsoUtc("2024-12-31T23:59:59Z", days, day_of_year, seconds));
    CHECK(day_of_year == 366);
    CHECK(seconds == 86399.0);
    CHECK(PhasedArrayLogParser::parseIsoUtc("2000-02-29T00:00:00Z", days, day_of_year, seconds));
    CHECK_FALSE(PhasedArrayLogParser::parseIsoUtc("2100-02-29T00:00:00Z", days, day_of_year, seconds));

    // The day number advances by one a day, across a month and a year.
    long long a = 0, b = 0, c = 0;
    int ignored = 0;
    REQUIRE(PhasedArrayLogParser::parseIsoUtc("2026-12-31T00:00:00Z", a, ignored, seconds));
    REQUIRE(PhasedArrayLogParser::parseIsoUtc("2027-01-01T00:00:00Z", b, ignored, seconds));
    REQUIRE(PhasedArrayLogParser::parseIsoUtc("2027-03-01T00:00:00Z", c, ignored, seconds));
    CHECK(b - a == 1);
    CHECK(c - b == 59);
}

TEST_CASE("PhasedArrayLog: a time that is not ISO 8601 UTC is refused")
{
    long long days = 7;
    int day_of_year = 7;
    double seconds = 7.0;
    for (const char* bad : {"", "2026-11-13T19:15:35", "2026-11-13T19:15:35.513", "2026-11-13 19:15:35.513Z", "2026-13-01T00:00:00Z",
                            "2026-11-31T00:00:00Z", "2026-11-13T24:00:00Z", "2026-11-13T19:60:00Z", "2026-11-13T19:15:60Z",
                            "2026-11-13T19:15:35.Z", "2026-11-13T19:15:35,513Z", "2026-11-13t19:15:35Z", "2026-11-13T19:15:3xZ",
                            "26-11-13T19:15:35Z", "2026-11-13T19:15:35+00:00", "119:15:35.513"})
    {
        CAPTURE(bad);
        CHECK_FALSE(PhasedArrayLogParser::parseIsoUtc(bad, days, day_of_year, seconds));
    }
    CHECK(days == 7);   // an outcome that is refused leaves the outputs as they were
    CHECK(day_of_year == 7);
    CHECK(seconds == 7.0);
}

TEST_CASE("PhasedArrayLog: each stream is three series, H, V and C, in the order a host plots them")
{
    TempDir dir;
    const CsvParseResult r = parseText(dir, logText({kHeader,
        "2026-11-13T19:15:35.500Z,SCU1,HPol,1.0,10.0",
        "2026-11-13T19:15:35.500Z,SCU1,VPol,2.0,20.0",
        "2026-11-13T19:15:35.500Z,SCU1,Comb,3.0,30.0",
        "2026-11-13T19:15:36.500Z,SCU1,HPol,1.5,10.5",
        "2026-11-13T19:15:36.500Z,SCU1,VPol,2.5,20.5",
        "2026-11-13T19:15:36.500Z,SCU1,Comb,3.5,30.5"}));

    REQUIRE(r.success);
    CHECK(r.skippedRows == 0);
    CHECK(r.baseDay == 317);
    CHECK(r.baseTimeOffset == doctest::Approx(19 * 3600 + 15 * 60 + 35.5));
    CHECK(r.xMax == doctest::Approx(1.0));
    REQUIRE(r.series.size() == 6);

    const char* names[6] = {"Stream 1 H", "Stream 1 V", "Stream 1 C", "Stream 2 H", "Stream 2 V", "Stream 2 C"};
    for (std::size_t i = 0; i < 6; ++i)
    {
        CAPTURE(i);
        const CsvSeries& s = r.series[i];
        CHECK(s.name == names[i]);
        CHECK(s.metricType == MetricType::SNR);
        CHECK(s.streamLabel == "SCU1");
        CHECK(s.receiverIndex == static_cast<int>(i / 3) + 1);   // the array's channel, 1-based
        CHECK(s.channelIndex == static_cast<int>(i % 3));        // 0 H, 1 V, 2 C
        CHECK(s.streamOrder == 1);
        CHECK(s.sourceId == 0);
        REQUIRE(s.xValues.size() == 2);
        CHECK(s.xValues[0] == 0.0);
        CHECK(s.xValues[1] == doctest::Approx(1.0));
    }
    CHECK(r.series[0].yValues == std::vector<double>{1.0, 1.5});     // Stream 1, HPol
    CHECK(r.series[1].yValues == std::vector<double>{2.0, 2.5});     // Stream 1, VPol
    CHECK(r.series[2].yValues == std::vector<double>{3.0, 3.5});     // Stream 1, Comb
    CHECK(r.series[5].yValues == std::vector<double>{30.0, 30.5});   // Stream 2, Comb
    CHECK(r.series[0].yMin == 1.0);
    CHECK(r.series[0].yMax == 1.5);
}

TEST_CASE("PhasedArrayLog: the order the three rows of a sample come in changes nothing")
{
    TempDir dir;
    const std::vector<std::string> rows = {"2026-11-13T19:15:35.500Z,SCU1,HPol,1.0,10.0", "2026-11-13T19:15:35.500Z,SCU1,VPol,2.0,20.0",
                                           "2026-11-13T19:15:35.500Z,SCU1,Comb,3.0,30.0"};
    std::vector<std::string> shuffled = {rows[2], rows[0], rows[1]};
    shuffled.insert(shuffled.begin(), kHeader);
    std::vector<std::string> ordered = rows;
    ordered.insert(ordered.begin(), kHeader);

    const CsvParseResult a = parseText(dir, logText(ordered));
    const CsvParseResult b = parseText(dir, logText(shuffled));
    REQUIRE(a.success);
    REQUIRE(b.success);
    REQUIRE(a.series.size() == b.series.size());
    for (std::size_t i = 0; i < a.series.size(); ++i)
    {
        CHECK(a.series[i].name == b.series[i].name);
        CHECK(a.series[i].yValues == b.series[i].yValues);
    }
}

TEST_CASE("PhasedArrayLog: a malformed row is skipped and counted, an empty cell is no sample")
{
    TempDir dir;
    const CsvParseResult r = parseText(dir, logText({kHeader,
        "2026-11-13T19:15:35.500Z,SCU1,HPol,1.0,10.0",
        "2026-11-13T19:15:36.500Z,SCU1,HPol,1.5",                 // too few columns
        "not a time,SCU1,HPol,1.5,10.5",                          // a time that is not ISO 8601
        "2026-11-13T19:15:37.500Z,SCU1,XPol,1.5,10.5",            // a polarization that is none of the three
        "2026-11-13T19:15:38.500Z,SCU1,HPol,,12.0",               // an empty cell: that stream has no sample here
        ""}));                                                     // a blank line is not a row

    REQUIRE(r.success);
    CHECK(r.skippedRows == 3);
    REQUIRE(r.series.size() == 2);   // only HPol of each stream had any sample
    CHECK(r.series[0].name == "Stream 1 H");
    CHECK(r.series[0].yValues == std::vector<double>{1.0});
    CHECK(r.series[1].name == "Stream 2 H");
    CHECK(r.series[1].yValues == std::vector<double>{10.0, 12.0});
    CHECK(r.series[1].xValues[1] == doctest::Approx(3.0));
}

TEST_CASE("PhasedArrayLog: a log of several units keeps them apart")
{
    TempDir dir;
    const CsvParseResult r = parseText(dir, logText({kHeader,
        "2026-11-13T19:15:35.500Z,SCU1,HPol,1.0,10.0",
        "2026-11-13T19:15:35.500Z,SCU2,HPol,5.0,50.0"}));

    REQUIRE(r.success);
    REQUIRE(r.series.size() == 4);
    CHECK(r.series[0].name == "SCU1 Stream 1 H");
    CHECK(r.series[0].streamLabel == "SCU1");
    CHECK(r.series[0].streamOrder == 1);
    CHECK(r.series[2].name == "SCU2 Stream 1 H");
    CHECK(r.series[2].streamLabel == "SCU2");
    CHECK(r.series[2].streamOrder == 2);
    CHECK(r.series[2].yValues == std::vector<double>{5.0});
}

TEST_CASE("PhasedArrayLog: time runs on across midnight and across a year's end")
{
    TempDir dir;
    const CsvParseResult r = parseText(dir, logText({"Time,Name,Polarity,Stream 1",
        "2026-12-31T23:59:59.000Z,SCU1,HPol,1.0",
        "2027-01-01T00:00:01.000Z,SCU1,HPol,2.0"}));

    REQUIRE(r.success);
    CHECK(r.baseDay == 365);
    CHECK(r.baseTimeOffset == 86399.0);
    REQUIRE(r.series.size() == 1);
    CHECK(r.series[0].xValues == std::vector<double>{0.0, 2.0});
    CHECK(r.xMax == 2.0);
}

TEST_CASE("PhasedArrayLog: the file is read as the application's other CSV is: a BOM, any line end")
{
    TempDir dir;
    const std::string body = kHeader + "\n2026-11-13T19:15:35.500Z,SCU1,HPol,1.0,10.0\n";
    const CsvParseResult plain = parseText(dir, body);
    const CsvParseResult bom = parseText(dir, "\xEF\xBB\xBF" + body);
    REQUIRE(plain.success);
    REQUIRE(bom.success);
    CHECK(bom.series.size() == plain.series.size());
    CHECK(bom.series[0].name == "Stream 1 H");   // the mark did not become part of the first header
}

TEST_CASE("PhasedArrayLog: what is not the log's layout is refused, not read as garbage")
{
    TempDir dir;
    const std::string row = "2026-11-13T19:15:35.500Z,SCU1,HPol,1.0,10.0";
    CHECK_FALSE(parseText(dir, logText({"Timestamp, Name, Polarity, Stream 1", row})).success);        // the first column
    CHECK_FALSE(parseText(dir, logText({"Time, Unit, Polarity, Stream 1", row})).success);             // the second
    CHECK_FALSE(parseText(dir, logText({"Time, Name, Pol, Stream 1", row})).success);                  // the third
    CHECK_FALSE(parseText(dir, logText({"Time, Name, Polarity", "2026-11-13T19:15:35.500Z,SCU1,HPol"})).success);   // no stream
    CHECK_FALSE(parseText(dir, logText({kHeader})).success);                                            // no data row
    CHECK_FALSE(parseText(dir, logText({kHeader, "garbage", "more garbage"})).success);                // no parseable row
    CHECK_FALSE(parseText(dir, "").success);
    CHECK_FALSE(PhasedArrayLogParser::parse(dir.file("missing.csv")).success);

    // The two CSV layouts are told apart by their header: each parser refuses the other's file.
    const fs::path ours = dir.file("ours.csv");
    writeBytes(ours, "Time (DOY:HH:MM:SS.mmm),A Lock (%)\r\n317:19:15:35.500,99.5\r\n");
    CHECK(CsvSeriesParser::parse(ours).success);
    CHECK_FALSE(PhasedArrayLogParser::parse(ours).success);
    const fs::path theirs = dir.file("theirs.csv");
    writeBytes(theirs, logText({kHeader, row}));
    CHECK(PhasedArrayLogParser::parse(theirs).success);
    CHECK_FALSE(CsvSeriesParser::parse(theirs).success);
}

TEST_CASE("PhasedArrayLog: the committed sample is 30 streams of three series, a sample a second for 41 minutes")
{
    const CsvParseResult r = PhasedArrayLogParser::parse(recordingFile("av_snr_log.csv"));

    REQUIRE(r.success);
    CHECK(r.skippedRows == 0);
    CHECK(r.baseDay == 317);   // 2026-11-13
    CHECK(r.baseTimeOffset == doctest::Approx(19 * 3600 + 15 * 60 + 35.513));
    CHECK(r.xMax == doctest::Approx((56 * 60 + 53.474) - (15 * 60 + 35.513)));   // to 19:56:53.474Z
    REQUIRE(r.series.size() == 90);

    CHECK(r.series.front().name == "Stream 1 H");
    CHECK(r.series.front().streamLabel == "SCU1");
    CHECK(r.series.back().name == "Stream 30 C");
    CHECK(r.series.back().receiverIndex == 30);
    CHECK(r.series.back().channelIndex == 2);

    double low = 1e9, high = -1e9;
    for (const CsvSeries& s : r.series)
    {
        CAPTURE(s.name);
        CHECK(s.xValues.size() == 2479);   // 7,437 rows in samples of three
        CHECK(s.yValues.size() == 2479);
        low = std::min(low, s.yMin);
        high = std::max(high, s.yMax);
        for (std::size_t i = 1; i < s.xValues.size(); ++i)
        {
            REQUIRE(s.xValues[i] >= s.xValues[i - 1]);   // time never goes backwards
        }
    }
    CHECK(low == doctest::Approx(-1.9));
    CHECK(high == doctest::Approx(19.0));
    // The first sample, read off the file: Stream 3 is 2.4 in H, -1.9 in V and -0.8 in C.
    CHECK(r.series[6].yValues.front() == doctest::Approx(2.4));    // Stream 3 H
    CHECK(r.series[7].yValues.front() == doctest::Approx(-1.9));   // Stream 3 V
    CHECK(r.series[8].yValues.front() == doctest::Approx(-0.8));   // Stream 3 C
}
