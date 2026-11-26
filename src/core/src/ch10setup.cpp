/**
 * @file ch10setup.cpp
 * @brief Implementation of the steps both Chapter 10 readers start a recording with.
 */

#include "ch10setup.h"

#include <cassert>
#include <cstddef>
#include <new>
#include <string>

#include "i106_time.h"

#include "constants.h"

using namespace Irig106;

namespace ch10setup {

const char* describe(Failure failure)
{
    switch (failure)
    {
    case Failure::None:        return "";
    case Failure::Open:        return "Error opening data file.";
    case Failure::TimeSync:    return "Error establishing time sync.";
    case Failure::FirstHeader: return "Failed to read first header.";
    case Failure::NotTmats:    return "Failed to find TMATS message.";
    case Failure::Memory:      return "Memory allocation failed.";
    case Failure::ReadTmats:   return "Failed to read TMATS data.";
    case Failure::TmatsLength: return "The TMATS packet's data length does not fit the packet: the recording is corrupt.";
    case Failure::DecodeTmats: return "Failed to decode TMATS.";
    }
    return "";
}

namespace {

/// The header's own length: the primary header, and the secondary one when the flags say there is one (irig106's
/// iGetHeaderLen(), which takes a non-const header).
std::uint32_t headerLength(const SuI106Ch10Header& header)
{
    return (header.ubyPacketFlags & I106CH10_PFLAGS_SEC_HEADER) != 0 ? HEADER_SIZE + SEC_HEADER_SIZE : HEADER_SIZE;
}

} // namespace

std::uint32_t bodyLength(const SuI106Ch10Header& header)
{
    const std::uint32_t own = headerLength(header);
    return header.ulPacketLen >= own ? header.ulPacketLen - own : 0;
}

PacketRead readPacketData(int handle, const SuI106Ch10Header& header, std::uint32_t min_data_length,
                          std::vector<std::uint8_t>& buffer, EnI106Status& status)
{
    const auto required = static_cast<std::size_t>(header.ulPacketLen);
    if (required > static_cast<std::size_t>(PCMConstants::kMaxPacketBufferSize))
    {
        return PacketRead::Memory;
    }
    // irig106 reads bodyLength() bytes into the buffer (which is at least the packet length) and never compares the
    // data length with it; whatever reads the data afterwards trusts the data length. So it must lie inside what is read.
    if (header.ulPacketLen < headerLength(header) || header.ulDataLen > bodyLength(header) ||
        header.ulDataLen < min_data_length)
    {
        return PacketRead::BadLength;
    }
    if (buffer.size() < required)
    {
        try
        {
            buffer.resize(required);
        }
        catch (const std::bad_alloc&)
        {
            return PacketRead::Memory;
        }
    }
    status = enI106Ch10ReadData(handle, static_cast<unsigned long>(buffer.size()), buffer.data());
    return status == I106_OK ? PacketRead::Ok : PacketRead::ReadError;
}

Failure openRecording(const Irig106SetupLock& setup_lock, const std::filesystem::path& path, int& handle,
                      EnI106Status& status)
{
    assert(setup_lock.owns_lock());
    (void)setup_lock;

    // The path in the process code page: irig106 opens with the CRT's narrow open(), which reads that code page - UTF-8
    // under the UTF-8 code-page manifest (src/TmDq.App/app.manifest, assets/utf8codepage.manifest) merged into the app
    // and every test host. (UTF-8 bytes whatever the code page would fail for a path the other reader opened.)
    const std::string narrow_path = path.string();
    status = enI106Ch10Open(&handle, narrow_path.c_str(), I106_READ);
    if (status != I106_OK && status != I106_OPEN_WARNING)
    {
        return Failure::Open;
    }

    // Establishes the time reference that enI106_Rel2IrigTime() converts relative header timestamps with.
    status = enI106_SyncTime(handle, bFALSE, 0);
    return status == I106_OK ? Failure::None : Failure::TimeSync;
}

Failure readTmats(const Irig106SetupLock& setup_lock, int handle, SuI106Ch10Header& header,
                  std::vector<std::uint8_t>& buffer, SuTmatsInfo& tmats, EnI106Status& status)
{
    assert(setup_lock.owns_lock());
    (void)setup_lock;

    // A valid Chapter 10 file's first packet is TMATS, which enumerates every channel; a file that does not begin with
    // one is not a processable stream.
    status = enI106Ch10ReadNextHeader(handle, &header);
    if (status != I106_OK)
    {
        return Failure::FirstHeader;
    }
    if (header.ubyDataType != I106CH10_DTYPE_TMATS)
    {
        return Failure::NotTmats;
    }
    // The decoder reads the channel-specific word, then ulDataLen - 4 bytes of text: both must be inside the packet.
    switch (readPacketData(handle, header, static_cast<std::uint32_t>(sizeof(SuTmats_ChanSpec)), buffer, status))
    {
    case PacketRead::Ok:
        break;
    case PacketRead::Memory:
        return Failure::Memory;
    case PacketRead::ReadError:
        return Failure::ReadTmats;
    case PacketRead::BadLength:
        return Failure::TmatsLength;
    }
    status = enI106_Decode_Tmats(&header, buffer.data(), &tmats);
    return status == I106_OK ? Failure::None : Failure::DecodeTmats;
}

} // namespace ch10setup
