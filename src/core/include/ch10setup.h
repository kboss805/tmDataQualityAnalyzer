/**
 * @file ch10setup.h
 * @brief What both Chapter 10 readers do to start a recording, and to read one packet, written once.
 *
 * `Chapter10Reader` (the channel list) and `Ch10PacketReader` (the run) begin the same way: open the file, establish
 * time, read the first packet, require it to be TMATS and decode it. They had each written that out, and the two had
 * come to differ: one refused a file irig106 only warns about, one capped its read buffer, one let a TMATS that would
 * not decode through. The steps are here now, and a reader differs from the other only in what it does with the result.
 *
 * Open and read-the-TMATS are two functions, not one, because the metadata reader clears its channels between them (a
 * file that does not open leaves the old ones alone).
 *
 * Every function that opens, syncs or decodes touches irig106's process-wide state, so each takes the setup lock
 * (irig106lock.h) by reference: the caller holds it for the whole setup. Reading a packet from an open handle needs no
 * lock.
 */

#ifndef CH10SETUP_H
#define CH10SETUP_H

#include <cstdint>
#include <filesystem>
#include <vector>

#include "irig106ch10.h"
#include "i106_decode_tmats.h"

#include "irig106lock.h"

namespace ch10setup {

/// The step of a setup that failed, if one did.
enum class Failure
{
    None,
    Open,         ///< The file would not open (irig106's "open warning" is not this: the file opened).
    TimeSync,     ///< Time could not be established.
    FirstHeader,  ///< The first packet's header could not be read.
    NotTmats,     ///< The first packet is not TMATS.
    Memory,       ///< The TMATS packet is larger than the buffer may grow to, or the buffer could not grow.
    ReadTmats,    ///< The TMATS packet's data could not be read.
    TmatsLength,  ///< The TMATS packet's header claims more data than the packet carries, or too little to be TMATS.
    DecodeTmats,  ///< The TMATS text did not decode.
};

/// A failure in words, as the operator reads it. `Failure::None` is the empty string.
[[nodiscard]] const char* describe(Failure failure);

/// How reading one packet's data ended.
enum class PacketRead
{
    Ok,
    Memory,     ///< The packet is larger than the buffer may grow to (100 MB), or the buffer could not grow.
    ReadError,  ///< irig106 could not read it.
    BadLength,  ///< The header's lengths contradict each other: a corrupt (or crafted) packet, not read.
};

/// The bytes of a packet that follow its header (data, filler and any data checksum): what irig106 reads into the
/// buffer. Zero for a packet length shorter than its own header.
[[nodiscard]] std::uint32_t bodyLength(const Irig106::SuI106Ch10Header& header);

/**
 * @brief Reads the data of the packet whose header @p header was just read, growing @p buffer to hold it.
 *
 * The buffer grows to the packet's whole length (its data is a little less) and never past
 * `PCMConstants::kMaxPacketBufferSize`: a header that claims more is refused rather than allocated.
 *
 * The header's data length (`ulDataLen`) is what the decoders, and the reader's own copy of a PCM packet, trust to say
 * how many bytes to read from the buffer; irig106 reads the packet length's worth and checks neither against the
 * other. So a data length longer than the bytes that follow the header (`bodyLength()`), or shorter than
 * @p min_data_length (a type's channel-specific word, which every decoder reads first), is refused as `BadLength`
 * before anything is read: it would have sent a decoder or a copy past the data, or the TMATS decoder's
 * `ulDataLen - 4` round to four billion.
 *
 * @param[in]     handle           An open irig106 handle, positioned just past @p header.
 * @param[in]     header           The packet's header.
 * @param[in]     min_data_length  The fewest data bytes a packet of this type can have (0: no floor).
 * @param[in,out] buffer           Grown if it is too small; the data is at its start.
 * @param[out]    status           What irig106 returned from the read (left alone when nothing was read).
 */
[[nodiscard]] PacketRead readPacketData(int handle, const Irig106::SuI106Ch10Header& header,
                                        std::uint32_t min_data_length, std::vector<std::uint8_t>& buffer,
                                        Irig106::EnI106Status& status);

/**
 * @brief Opens @p path for reading and establishes time.
 *
 * An "open warning" is accepted: irig106 gives it for a file whose first packet it cannot read or that does not begin
 * with a computer-generated packet, and opens the file all the same. The steps after this one say what is wrong.
 *
 * @param[out] handle  The irig106 handle. Open whenever the result is not `Failure::Open`, `TimeSync` included: close it.
 * @param[out] status  What irig106 last returned.
 * @return `None`, `Open` or `TimeSync`.
 */
[[nodiscard]] Failure openRecording(const Irig106SetupLock& setup_lock, const std::filesystem::path& path, int& handle,
                                    Irig106::EnI106Status& status);

/**
 * @brief Reads the first packet, requires it to be TMATS, and decodes it into @p tmats.
 *
 * @p tmats is not zeroed first: the decoder frees whatever tree an earlier decode left in it (a new one starts zeroed),
 * and zeroing it would orphan that tree.
 *
 * @param[in]     handle  The handle `openRecording()` gave, still at the start of the file.
 * @param[out]    header  The first packet's header.
 * @param[in,out] buffer  Holds the TMATS packet's data afterwards.
 * @param[in,out] tmats   Receives the decoded TMATS.
 * @param[out]    status  What irig106 last returned.
 * @return `None`, `FirstHeader`, `NotTmats`, `Memory`, `ReadTmats`, `TmatsLength` or `DecodeTmats`.
 */
[[nodiscard]] Failure readTmats(const Irig106SetupLock& setup_lock, int handle, Irig106::SuI106Ch10Header& header,
                                std::vector<std::uint8_t>& buffer, Irig106::SuTmatsInfo& tmats,
                                Irig106::EnI106Status& status);

} // namespace ch10setup

#endif // CH10SETUP_H
