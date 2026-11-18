// tmatswalk: the walk down a decoded TMATS tree's data sources, and the channel a data source names, on trees built by
// hand (follow-up audit S3: three loops had each walked the tree and read the track number their own way).

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "doctest.h"
#include "i106_decode_tmats.h"
#include "tmatswalk.h"

namespace {

using Irig106::SuRDataSource;
using Irig106::SuRRecord;
using Irig106::SuTmatsInfo;

/// A tree of R records of data sources, linked as the decoder links them; the track numbers are the owners' text.
struct Tree
{
    std::vector<std::vector<std::string>> tracks;   ///< one list of track numbers per R record
    std::vector<SuRRecord>                records;
    std::vector<SuRDataSource>            sources;
    SuTmatsInfo                           info{};
    std::vector<std::string>              text;

    explicit Tree(std::vector<std::vector<std::string>> per_record) : tracks(std::move(per_record))
    {
        std::size_t total = 0;
        for (const auto& r : tracks) total += r.size();
        records.assign(tracks.size(), SuRRecord{});
        sources.assign(total, SuRDataSource{});
        text.reserve(total);
        std::size_t next = 0;
        for (std::size_t r = 0; r < tracks.size(); ++r)
        {
            SuRDataSource* previous = nullptr;
            for (const std::string& track : tracks[r])
            {
                SuRDataSource& source = sources[next++];
                text.push_back(track);
                source.szTrackNumber = (track == "<none>") ? nullptr : text.back().data();
                if (previous != nullptr) previous->psuNext = &source;
                else records[r].psuFirstDataSource = &source;
                previous = &source;
            }
            if (r > 0) records[r - 1].psuNext = &records[r];
        }
        info.psuFirstRRecord = records.empty() ? nullptr : &records[0];
    }
};

/// The track numbers a walk visits, in order, with "-" for a data source that names no channel.
std::vector<std::string> walked(const Tree& tree, std::size_t stop_after = 0)
{
    std::vector<std::string> out;
    tmatswalk::forEachDataSource(tree.info, [&](const SuRDataSource& source) {
        const std::optional<int> track = tmatswalk::trackNumber(source);
        out.push_back(track.has_value() ? std::to_string(*track) : "-");
        return (stop_after != 0 && out.size() == stop_after) ? tmatswalk::Walk::Stop : tmatswalk::Walk::Continue;
    });
    return out;
}

} // namespace

TEST_CASE("TmatsWalk: every data source of every R record is visited, in the tree's order")
{
    const Tree tree({{"1", "2"}, {"3"}, {"4", "5", "6"}});
    CHECK(walked(tree) == std::vector<std::string>{"1", "2", "3", "4", "5", "6"});
}

TEST_CASE("TmatsWalk: a walk stops when the visitor says so, and not before")
{
    const Tree tree({{"1", "2"}, {"3"}, {"4", "5", "6"}});
    CHECK(walked(tree, 1) == std::vector<std::string>{"1"});
    CHECK(walked(tree, 3) == std::vector<std::string>{"1", "2", "3"});   // at the end of the second record
    CHECK(walked(tree, 6) == std::vector<std::string>{"1", "2", "3", "4", "5", "6"});
}

TEST_CASE("TmatsWalk: a tree with no R records, or R records with no data sources, visits nothing")
{
    CHECK(walked(Tree({})).empty());
    CHECK(walked(Tree({{}, {}})).empty());
    CHECK(walked(Tree({{}, {"7"}, {}})) == std::vector<std::string>{"7"});
}

TEST_CASE("TmatsWalk: a track number is the channel the data source names, or none")
{
    // Absent (a null field, "<none>" here), empty, blank, negative, text, a number with something after it and one too big
    // for an int name no channel; digits read as the number, with surrounding whitespace, a leading '+' and zeros.
    const Tree tree({{"<none>", "", " ", "-1", "-27", "abc", "7x", "99999999999", "0", "5", "05", " 12 ", "+5"}});
    CHECK(walked(tree) == std::vector<std::string>{"-", "-", "-", "-", "-", "-", "-", "-", "0", "5", "5", "12", "5"});
}
