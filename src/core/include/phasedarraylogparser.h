/**
 * @file phasedarraylogparser.h
 * @brief Parser for a phased array antenna's SNR log: a CSV another application writes (US10.0).
 *
 * The log has one row per measurement: `Time, Name, Polarity, Stream 1, ..., Stream N`. The time is ISO 8601 in UTC
 * (`2026-11-13T19:15:35.513Z`), the name is the unit that logged it, the polarization is `HPol`, `VPol` or `Comb`, and
 * each stream column holds that stream's value for that polarization at that time: a sample is three rows with the same
 * time. Spaces after the commas are accepted.
 *
 * It yields the same `CsvParseResult` the application's own CSV does, so a host reads either through one set of
 * accessors. Each stream of each unit becomes three series, H, V and C: `receiverIndex` is the 1-based stream number
 * (the array's channel, not a receiver of this application), `channelIndex` the polarization (0 H, 1 V, 2 C) and
 * `streamOrder` the 1-based unit, so a log of several units keeps them apart. Pure data ingestion, thread-safe, no UI.
 */

#ifndef PHASEDARRAYLOGPARSER_H
#define PHASEDARRAYLOGPARSER_H

#include <filesystem>
#include <string_view>

#include "csvseriesparser.h"

class PhasedArrayLogParser
{
public:
    /// The polarization letters in `channelIndex` order: H (horizontal), V (vertical), C (combined).
    static constexpr char kPolarizationLetters[3] = {'H', 'V', 'C'};

    /// Parses the log. @c success is false for an unreadable file, a header that is not the log's, or a file with no
    /// parseable sample; @c skippedRows counts the data rows left out (too few columns, a time that is not ISO 8601 UTC,
    /// or a polarization that is none of the three).
    static CsvParseResult parse(const std::filesystem::path& path);

    /// A time `YYYY-MM-DDTHH:MM:SS[.fff]Z` as the number of days since 1970-01-01, the day of the year (1-based) and the
    /// seconds into the day. False, leaving the outputs untouched, when it is not one or a field is out of range.
    static bool parseIsoUtc(std::string_view stamp, long long& out_days, int& out_day_of_year, double& out_seconds);
};

#endif // PHASEDARRAYLOGPARSER_H
