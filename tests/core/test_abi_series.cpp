// Series construction through the C ABI (src/core/abi/abi_series.cpp): a stream becomes the series the core builds, the time
// base, stream keys, imported stream orders.

#include "test_abi_support.h"

TEST_CASE("Abi: a stream becomes the series the core builds")
{
    std::mt19937 rng(20261024);
    int withLock = 0, withMissed = 0, withChannels = 0, empty = 0, nan = 0;
    for (int i = 0; i < 1500; ++i)
    {
        const ProcessedStreamData data = randomStream(rng);
        const double base = (rng() % 3 == 0) ? 0.0 : 1.7e9 + double(rng() % 400) / 4.0;
        const StreamSeries want = buildStreamSeries(data, base);

        tmdq_stream_result* result = makeResult(data);
        tmdq_series_set* set = nullptr;
        REQUIRE(tmdq_stream_result_series(result, base, &set) == int(TMDQ_OK));
        REQUIRE(set != nullptr);
        const std::string diff = seriesDifferences(set, want);
        REQUIRE_MESSAGE(diff.empty(), (std::format("case {}: ", i) + diff));
        tmdq_series_set_free(set);
        tmdq_stream_result_free(result);

        withLock += !data.lockPercent.empty();
        withMissed += !data.accumulatedMissedFrames.empty();
        withChannels += !data.channels.empty();
        empty += want.series.empty();
        for (const SeriesSpec& s : want.series)
            nan += std::isnan(s.yMin) || std::isinf(s.yMax);
    }
    // The generator really reached each kind of stream.
    REQUIRE_MESSAGE((withLock > 800 && withMissed > 700 && withChannels > 900 && empty > 20 && nan > 20), std::format("{} {} {} {} {}", withLock, withMissed, withChannels, empty, nan));

    // A known stream, so the oracle is not the only witness: lock %, missed frames and two receiver channels.
    ProcessedStreamData known;
    known.streamLabel = "Rcvr";
    known.pcmChannelId = 40;
    known.jobIndex = 2;
    known.sourceId = 1;
    known.timesSec = { 1000.0, 1001.0 };
    known.lockPercent = { 99.0, 98.0 };
    known.accumulatedMissedFrames = { 0.0, 4.0 };
    known.channels.push_back({"L_RCVR1", 1, {-80.0, -79.0}});
    known.channels.push_back({"R_RCVR2", 2, {-70.0, -71.0}});
    tmdq_stream_result* result = makeResult(known);
    tmdq_series_set* set = nullptr;
    REQUIRE(tmdq_stream_result_series(result, 990.0, &set) == int(TMDQ_OK));
    tmdq_series_set_info info{};
    info.struct_size = sizeof(info);
    REQUIRE(tmdq_series_set_info_get(set, &info) == int(TMDQ_OK));
    REQUIRE(info.series_count == 4);
    REQUIRE(info.elapsed_count == 2);
    CHECK(info.elapsed[0] == 10.0);
    CHECK(info.elapsed[1] == 11.0);
    tmdq_series_spec spec{};
    spec.struct_size = sizeof(spec);
    REQUIRE(tmdq_series_set_spec(set, 3, &spec) == int(TMDQ_OK));
    CHECK(spec.receiver_index == 2);
    CHECK(spec.stream_order == 40);
    CHECK(spec.stream_sequence == 2);
    CHECK(spec.source_id == 1);
    CHECK(spec.y_min == -71.0);
    CHECK(spec.y_max == -70.0);

    // The set keeps the stream's data alive after the host has freed the result. Reading the values alone cannot
    // show it: a Release heap leaves freed memory as it was (a Debug heap overwrites it), so
    // the data's lifetime is watched directly, and it must end with the set.
    const std::weak_ptr<const ProcessedStreamData> data = result->data;
    tmdq_stream_result_free(result);
    REQUIRE(!data.expired());
    CHECK(spec.y_values[0] == -70.0);
    REQUIRE(tmdq_series_set_spec(set, 2, &spec) == int(TMDQ_OK));
    CHECK(spec.y_values[1] == -79.0);
    CHECK(std::string(spec.name_utf8).empty() == false);
    tmdq_series_set_free(set);
    CHECK(data.expired());

    // Arguments.
    tmdq_series_set* none = reinterpret_cast<tmdq_series_set*>(std::uintptr_t{1});
    CHECK(tmdq_stream_result_series(nullptr, 0.0, &none) == int(TMDQ_ERR_BADARG));
    REQUIRE(none == nullptr);
    tmdq_stream_result* again = makeResult(known);
    CHECK(tmdq_stream_result_series(again, 0.0, nullptr) == int(TMDQ_ERR_BADARG));
    REQUIRE(tmdq_stream_result_series(again, 0.0, &set) == int(TMDQ_OK));
    CHECK(tmdq_series_set_info_get(nullptr, &info) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_series_set_info_get(set, nullptr) == int(TMDQ_ERR_BADARG));
    info.struct_size = sizeof(info) - 1;
    CHECK(tmdq_series_set_info_get(set, &info) == int(TMDQ_ERR_BADARG));
    spec.struct_size = sizeof(spec);
    CHECK(tmdq_series_set_spec(set, 4, &spec) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_series_set_spec(set, -1, &spec) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_series_set_spec(set, 0, nullptr) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_series_set_spec(nullptr, 0, &spec) == int(TMDQ_ERR_BADARG));
    spec.struct_size = sizeof(spec) - 1;
    CHECK(tmdq_series_set_spec(set, 0, &spec) == int(TMDQ_ERR_BADARG));
    tmdq_series_set_free(set);
    tmdq_series_set_free(nullptr);
    tmdq_stream_result_free(again);
}

TEST_CASE("Abi: the time base moves like the core")
{
    std::mt19937 rng(20261024);
    auto info = [](const tmdq_time_base* b) {
        tmdq_time_base_info i{};
        i.struct_size = sizeof(i);
        tmdq_time_base_info_get(b, &i);
        return i;
    };
    int adopted = 0, shifted = 0, advanced = 0, moved = 0, overlapped = 0, apart = 0;
    for (int run = 0; run < 700; ++run)
    {
        tmdq_time_base* abi = nullptr;
        REQUIRE(tmdq_time_base_create(&abi) == int(TMDQ_OK));
        SeriesTimeBase core;
        for (int step = 0; step < 14; ++step)
        {
            const int op = int(rng() % 6);
            if (op == 0)
            {
                REQUIRE(tmdq_time_base_reset(abi) == int(TMDQ_OK));
                core.reset();
            }
            else if (op == 1)
            {
                const int day = int(rng() % 400) - 5;
                const double offset = double(rng() % 86400) / 3.0;
                REQUIRE(tmdq_time_base_set_clock(abi, day, offset) == int(TMDQ_OK));
                core.setClock(day, offset);
            }
            else if (op == 2 || op == 3)
            {
                const double first = (rng() % 5 == 0) ? 0.0 : 1.7e9 + double(rng() % 100000) / 8.0 + (rng() % 2) * 86400.0 * 400;
                const bool has = rng() % 2;
                double shift = -1.0;
                REQUIRE(tmdq_time_base_adopt(abi, first, has ? 1 : 0, &shift) == int(TMDQ_OK));
                const double want = core.adopt(first, has);
                CHECK(shift == want);
                ++adopted;
                shifted += want != 0.0;
            }
            else if (op == 4)
            {
                const bool has = rng() % 3 != 0;
                const double earliest = double(int(rng() % 4000) - 500) / 8.0;
                double shift = -1.0;
                REQUIRE(tmdq_time_base_advance(abi, has ? 1 : 0, earliest, &shift) == int(TMDQ_OK));
                const double want = core.advance(has ? std::optional<double>(earliest) : std::nullopt);
                CHECK(shift == want);
                ++advanced;
                moved += want != 0.0;
            }
            else
            {
                // Around the base, so the new source sometimes touches what is loaded and sometimes does not.
                const double lo = core.baseAbsSeconds() + double(int(rng() % 600) - 200) / 4.0;
                const double hi = lo + double(rng() % 200) / 4.0;
                const double existing = double(rng() % 200) / 4.0;
                int32_t out = -1;
                REQUIRE(tmdq_time_base_overlaps(abi, lo, hi, existing, &out) == int(TMDQ_OK));
                CHECK((out != 0) == core.overlaps(lo, hi, existing));
                ++(out ? overlapped : apart);
            }
            const tmdq_time_base_info i = info(abi);
            CHECK(i.base_day == core.baseDay());
            CHECK(i.base_abs_seconds == core.baseAbsSeconds());
            CHECK(i.base_time_offset == core.baseTimeOffset());
        }
        tmdq_time_base_free(abi);
    }
    REQUIRE_MESSAGE((adopted > 2000 && shifted > 200 && advanced > 1000 && moved > 100 && overlapped > 100 && apart > 100), std::format("{} {} {} {} {} {}", adopted, shifted, advanced, moved, overlapped, apart));

    // Arguments.
    tmdq_time_base* base = reinterpret_cast<tmdq_time_base*>(std::uintptr_t{1});
    CHECK(tmdq_time_base_create(nullptr) == int(TMDQ_ERR_BADARG));
    REQUIRE(tmdq_time_base_create(&base) == int(TMDQ_OK));
    tmdq_time_base_info bi{};
    bi.struct_size = sizeof(bi);
    REQUIRE(tmdq_time_base_info_get(base, &bi) == int(TMDQ_OK));
    CHECK(bi.base_day == 0);
    CHECK(bi.base_abs_seconds == 0.0);
    CHECK(bi.base_time_offset == 0.0);
    double shift = 0.0;
    int32_t out = 0;
    CHECK(tmdq_time_base_info_get(nullptr, &bi) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_time_base_info_get(base, nullptr) == int(TMDQ_ERR_BADARG));
    bi.struct_size = sizeof(bi) - 1;
    CHECK(tmdq_time_base_info_get(base, &bi) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_time_base_reset(nullptr) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_time_base_set_clock(nullptr, 1, 2.0) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_time_base_adopt(nullptr, 1.0, 1, &shift) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_time_base_adopt(base, 1.0, 1, nullptr) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_time_base_advance(nullptr, 1, 1.0, &shift) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_time_base_advance(base, 1, 1.0, nullptr) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_time_base_overlaps(nullptr, 0.0, 1.0, 1.0, &out) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_time_base_overlaps(base, 0.0, 1.0, 1.0, nullptr) == int(TMDQ_ERR_BADARG));
    tmdq_time_base_free(base);
    tmdq_time_base_free(nullptr);
}

TEST_CASE("Abi: stream keys identify the same stream")
{
    ProcessedStreamData a;
    a.sourceId = 1;
    a.streamLabel = "Ch 13";
    a.pcmChannelId = 13;
    tmdq_stream_result* ra = makeResult(a);
    tmdq_stream_key ka{};
    ka.struct_size = sizeof(ka);
    REQUIRE(tmdq_stream_result_key(ra, &ka) == int(TMDQ_OK));
    CHECK(ka.source_id == 1);
    CHECK(std::string(ka.label_utf8) == std::string("Ch 13"));
    CHECK(ka.channel == 13);

    // Every difference of one field is a different stream; none is the same one.
    auto keyOf = [](ProcessedStreamData d, tmdq_stream_result** keep) {
        *keep = makeResult(d);
        tmdq_stream_key k{};
        k.struct_size = sizeof(k);
        tmdq_stream_result_key(*keep, &k);
        return k;
    };
    tmdq_stream_result *r1 = nullptr, *r2 = nullptr, *r3 = nullptr, *r4 = nullptr;
    ProcessedStreamData same = a;
    ProcessedStreamData otherSource = a; otherSource.sourceId = 2;
    ProcessedStreamData otherLabel = a; otherLabel.streamLabel = "Ch 14";
    ProcessedStreamData otherChannel = a; otherChannel.pcmChannelId = 14;
    const tmdq_stream_key ksame = keyOf(same, &r1), ksource = keyOf(otherSource, &r2), klabel = keyOf(otherLabel, &r3),
                          kchannel = keyOf(otherChannel, &r4);
    CHECK(tmdq_stream_key_equal(&ka, &ksame) == 1);
    CHECK(tmdq_stream_key_equal(&ka, &ka) == 1);
    CHECK(tmdq_stream_key_equal(&ka, &ksource) == 0);
    CHECK(tmdq_stream_key_equal(&ka, &klabel) == 0);
    CHECK(tmdq_stream_key_equal(&ka, &kchannel) == 0);
    CHECK(tmdq_stream_key_equal(&ksource, &ka) == 0);
    // The same answers as the core's own key.
    REQUIRE(streamKeyOf(a) == streamKeyOf(same));
    REQUIRE(!(streamKeyOf(a) == streamKeyOf(otherSource)));

    // A NULL label reads as empty; arguments.
    tmdq_stream_key nolabel = ka;
    nolabel.label_utf8 = nullptr;
    tmdq_stream_key emptyLabel = ka;
    emptyLabel.label_utf8 = "";
    CHECK(tmdq_stream_key_equal(&nolabel, &emptyLabel) == 1);
    CHECK(tmdq_stream_key_equal(nullptr, &ka) == -1);
    CHECK(tmdq_stream_key_equal(&ka, nullptr) == -1);
    tmdq_stream_key shortKey = ka;
    shortKey.struct_size = sizeof(shortKey) - 1;
    CHECK(tmdq_stream_key_equal(&shortKey, &ka) == -1);
    CHECK(tmdq_stream_key_equal(&ka, &shortKey) == -1);
    CHECK(tmdq_stream_result_key(nullptr, &ka) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_stream_result_key(ra, nullptr) == int(TMDQ_ERR_BADARG));
    ka.struct_size = sizeof(ka) - 1;
    CHECK(tmdq_stream_result_key(ra, &ka) == int(TMDQ_ERR_BADARG));
    for (tmdq_stream_result* r : { ra, r1, r2, r3, r4 })
        tmdq_stream_result_free(r);
}

TEST_CASE("Abi: imported stream orders are assigned like the core")
{
    std::mt19937 rng(20261024);
    static const char* const labels[] = { "A", "B", "", "R\xC3\xA9", "same" };
    int lockWithSibling = 0, lockWithoutSibling = 0, snr = 0;
    for (int run = 0; run < 1500; ++run)
    {
        const int n = int(rng() % 9);
        std::vector<ImportedSeries> want;
        for (int i = 0; i < n; ++i)
        {
            ImportedSeries s;
            s.metricType = MetricType(rng() % 3);
            s.sourceId = int(rng() % 3);
            s.streamLabel = labels[rng() % 5];
            s.streamOrder = s.metricType == MetricType::SNR ? int(rng() % 30) : int(rng() % 4);
            want.push_back(s);
        }
        std::vector<tmdq_imported_series> wire(want.size());
        std::vector<tmdq_imported_series*> pointers;
        for (std::size_t i = 0; i < want.size(); ++i)
        {
            wire[i].struct_size = sizeof(tmdq_imported_series);
            wire[i].metric_type = int(want[i].metricType);
            wire[i].stream_label_utf8 = want[i].streamLabel.c_str();
            wire[i].source_id = want[i].sourceId;
            wire[i].stream_order = want[i].streamOrder;
            pointers.push_back(&wire[i]);
        }
        const std::vector<ImportedSeries> before = want;
        assignImportedStreamOrders(want);
        REQUIRE(tmdq_assign_imported_stream_orders(pointers.empty() ? nullptr : pointers.data(), int(pointers.size())) == int(TMDQ_OK));
        for (std::size_t i = 0; i < want.size(); ++i)
        {
            REQUIRE_MESSAGE(wire[i].stream_order == want[i].streamOrder, std::format("run {} series {}", run, i));
            // Only the order is written.
            CHECK(wire[i].metric_type == int(before[i].metricType));
            CHECK(wire[i].source_id == before[i].sourceId);
            CHECK(std::string(wire[i].stream_label_utf8) == before[i].streamLabel);
            if (before[i].metricType == MetricType::SNR)
            {
                ++snr;
                continue;
            }
            // Whether a lock or missed-frames series has an SNR series of the same label in the same source.
            bool sibling = false;
            for (const ImportedSeries& other : before)
                sibling = sibling || (other.metricType == MetricType::SNR && other.sourceId == before[i].sourceId &&
                                      other.streamLabel == before[i].streamLabel);
            ++(sibling ? lockWithSibling : lockWithoutSibling);
        }
    }
    REQUIRE_MESSAGE((snr > 1000 && lockWithSibling > 300 && lockWithoutSibling > 500), std::format("{} {} {}", snr, lockWithSibling, lockWithoutSibling));

    // Arguments.
    tmdq_imported_series one{};
    one.struct_size = sizeof(one);
    one.metric_type = TMDQ_METRIC_FRAME_SYNC_LOCK;
    one.stream_label_utf8 = "x";
    tmdq_imported_series* list[] = { &one };
    REQUIRE(tmdq_assign_imported_stream_orders(list, 1) == int(TMDQ_OK));
    REQUIRE(tmdq_assign_imported_stream_orders(nullptr, 0) == int(TMDQ_OK));   // nothing to order
    CHECK(tmdq_assign_imported_stream_orders(list, -1) == int(TMDQ_ERR_BADARG));
    CHECK(tmdq_assign_imported_stream_orders(nullptr, 1) == int(TMDQ_ERR_BADARG));
    tmdq_imported_series* withNull[] = { &one, nullptr };
    CHECK(tmdq_assign_imported_stream_orders(withNull, 2) == int(TMDQ_ERR_BADARG));
    REQUIRE(std::string(tmdq_last_error()).find("series 1") != std::string::npos);
    tmdq_imported_series shortOne = one;
    shortOne.struct_size = sizeof(shortOne) - 1;
    tmdq_imported_series* withShort[] = { &shortOne };
    CHECK(tmdq_assign_imported_stream_orders(withShort, 1) == int(TMDQ_ERR_BADARG));
    tmdq_imported_series badMetric = one;
    badMetric.metric_type = 7;
    tmdq_imported_series* withBad[] = { &one, &badMetric };
    const int before = one.stream_order;
    CHECK(tmdq_assign_imported_stream_orders(withBad, 2) == int(TMDQ_ERR_BADARG));
    CHECK(one.stream_order == before);   // a refused list is left alone
    REQUIRE(std::string(tmdq_last_error()).find("metric") != std::string::npos);
}
