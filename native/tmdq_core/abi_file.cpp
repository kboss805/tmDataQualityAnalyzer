/**
 * @file abi_file.cpp
 * @brief The file handle of the C ABI: a recording whose TMATS has been read, with its channels.
 */

#include "tmdq_abi.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "abi_internal.h"
#include "chapter10reader.h"

namespace {

/// The first revision of tmdq_channel_info is the whole of it so far.
constexpr std::size_t kChannelInfoMinimum = sizeof(tmdq_channel_info);

struct ChannelRecord
{
    int         id = -1;
    std::string name;
    double      dataRateBps = 0.0;
};

} // namespace

/// A loaded recording. The names are copied out of the reader so the pointers handed to the host stay
/// valid for as long as the handle does, and the reader (and the irig106 state it holds) is gone as soon
/// as the channels are read.
struct tmdq_file
{
    std::vector<ChannelRecord> timeChannels;
    std::vector<ChannelRecord> pcmChannels;
    int                        defaultTimeChannel = -1;
};

namespace {

int32_t describe(const std::vector<ChannelRecord>& list, int32_t index, tmdq_channel_info* out)
{
    tmdq::clearError();
    // A negative index wraps to a huge size_t, so the one comparison refuses both ends.
    if (static_cast<std::size_t>(index) >= list.size())
    {
        tmdq::setError("channel index out of range");
        return TMDQ_ERR_BADARG;
    }
    const ChannelRecord& record = list[static_cast<std::size_t>(index)];
    tmdq_channel_info info{};
    info.channel_id          = record.id;
    info.tmats_data_rate_bps = record.dataRateBps;
    info.name_utf8           = record.name.c_str();
    if (!tmdq::writeStruct(info, kChannelInfoMinimum, out))
    {
        tmdq::setError("null output or a struct_size that is too small");
        return TMDQ_ERR_BADARG;
    }
    return TMDQ_OK;
}

} // namespace

int32_t tmdq_file_open(const char* path_utf8, tmdq_file** out)
{
    tmdq::clearError();
    if (!out)
    {
        tmdq::setError("null output");
        return TMDQ_ERR_BADARG;
    }
    *out = nullptr;
    if (!path_utf8)
    {
        tmdq::setError("null filename");
        return TMDQ_ERR_BADARG;
    }

    return tmdq::guarded("tmdq_file_open", [&]() -> int32_t {
        // Concurrent hosts are safe: the reader serialises its own setup (irig106lock.h), because the
        // vendored irig106 library's handle table and TMATS decoder are not thread-safe.
        Chapter10Reader reader;
        if (!reader.loadChannels(tmdq::pathFromUtf8(path_utf8)))
        {
            tmdq::setError(reader.lastError().empty()
                               ? std::string("Chapter10Reader::loadChannels failed - unreadable file or no TMATS")
                               : reader.lastError());
            return TMDQ_ERR_PREPARE;
        }

        auto file = std::make_unique<tmdq_file>();
        for (const auto& [id, name] : reader.getTimeChannelList())
        {
            file->timeChannels.push_back({id, name, 0.0});
        }
        for (const auto& [id, name] : reader.getPCMChannelList())
        {
            file->pcmChannels.push_back({id, name, reader.getTmatsDataRateBps(id)});
        }
        file->defaultTimeChannel = reader.getCurrentTimeChannelID();
        *out = file.release();
        return TMDQ_OK;
    });
}

void tmdq_file_close(tmdq_file* file) { delete file; }

int32_t tmdq_file_time_channel_count(const tmdq_file* file)
{
    return file ? static_cast<int32_t>(file->timeChannels.size()) : -1;
}

int32_t tmdq_file_pcm_channel_count(const tmdq_file* file)
{
    return file ? static_cast<int32_t>(file->pcmChannels.size()) : -1;
}

int32_t tmdq_file_default_time_channel(const tmdq_file* file) { return file ? file->defaultTimeChannel : -1; }

int32_t tmdq_file_time_channel(const tmdq_file* file, int32_t index, tmdq_channel_info* out)
{
    if (!file)
    {
        tmdq::setError("null file");
        return TMDQ_ERR_BADARG;
    }
    return describe(file->timeChannels, index, out);
}

int32_t tmdq_file_pcm_channel(const tmdq_file* file, int32_t index, tmdq_channel_info* out)
{
    if (!file)
    {
        tmdq::setError("null file");
        return TMDQ_ERR_BADARG;
    }
    return describe(file->pcmChannels, index, out);
}
