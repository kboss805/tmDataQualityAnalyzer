/**
 * @file tmatswalk.h
 * @brief The walk down a decoded TMATS tree's R records to their data sources, and the channel a data source names.
 *
 * Three places walked R record -> data source by hand (the metadata reader's channel names, its data rate lookup and the
 * packet reader's attribute table), and each read the data source's track number its own way. Both are here once.
 */

#ifndef TMATSWALK_H
#define TMATSWALK_H

#include <cstdlib>
#include <optional>

#include "i106_decode_tmats.h"

namespace tmatswalk {

/// What a visitor says after a data source: go on to the next one, or stop the walk.
enum class Walk
{
    Continue,
    Stop,
};

/// Calls @p visit with every data source of every R record, in the tree's order, until it returns `Walk::Stop`.
template <class Visit>
void forEachDataSource(const Irig106::SuTmatsInfo& info, Visit&& visit)
{
    for (Irig106::SuRRecord* record = info.psuFirstRRecord; record != nullptr; record = record->psuNext)
    {
        for (Irig106::SuRDataSource* source = record->psuFirstDataSource; source != nullptr; source = source->psuNext)
        {
            if (visit(*source) == Walk::Stop)
            {
                return;
            }
        }
    }
}

/**
 * @brief The channel a data source is recorded on (its TMATS track number), or no value if it names none.
 *
 * A data source with no track number names none, and neither does a negative one: a channel id is not negative, and a
 * negative number would index out of bounds. Text reads as 0, as `atoi` reads it - a malformed number is not told from
 * channel 0 here.
 */
[[nodiscard]] inline std::optional<int> trackNumber(const Irig106::SuRDataSource& source)
{
    if (source.szTrackNumber == nullptr)
    {
        return std::nullopt;
    }
    const int track = atoi(source.szTrackNumber);   // NOLINT(bugprone-unchecked-string-to-number-conversion): see above: text is 0
    if (track < 0)
    {
        return std::nullopt;
    }
    return track;
}

} // namespace tmatswalk

#endif // TMATSWALK_H
