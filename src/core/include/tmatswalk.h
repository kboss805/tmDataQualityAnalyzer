/**
 * @file tmatswalk.h
 * @brief The walk down a decoded TMATS tree's R records to their data sources, and the channel a data source names.
 *
 * Three places walked R record -> data source by hand (the metadata reader's channel names, its data rate lookup and the
 * packet reader's attribute table), and each read the data source's track number its own way. Both are here once.
 */

#ifndef TMATSWALK_H
#define TMATSWALK_H

#include <optional>
#include <string>

#include "i106_decode_tmats.h"

#include "textparse.h"

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
 * A data source with no track number names none, and neither does one that is not a whole decimal number (empty, text,
 * a number with something after it, one too big for an int) or that is negative: a channel id is not negative, and a
 * negative number would index out of bounds. The number is read as the rest of the application reads one
 * (`textparse::parseInteger`: surrounding whitespace and a leading '+' are fine). It was read with `atoi`, which reads
 * "abc" and an empty field as channel 0, so a malformed track number named the first channel.
 */
[[nodiscard]] inline std::optional<int> trackNumber(const Irig106::SuRDataSource& source)
{
    if (source.szTrackNumber == nullptr)
    {
        return std::nullopt;
    }
    int track = 0;
    if (!textparse::parseInteger(std::string(source.szTrackNumber), track) || track < 0)
    {
        return std::nullopt;
    }
    return track;
}

} // namespace tmatswalk

#endif // TMATSWALK_H
