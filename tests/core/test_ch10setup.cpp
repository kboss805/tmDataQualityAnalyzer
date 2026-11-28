// ch10setup: the steps both Chapter 10 readers start a recording with, tested directly (follow-up audit B9 and S3).
//
// The two readers used to write these steps out each, and had come to differ about what irig106 only warns about, about
// how big a packet may be, and about a TMATS that would not read. Here each step is given a file that fails it, and the
// reader tests hold the two readers to the same answer.

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "ch10setup.h"
#include "constants.h"
#include "doctest.h"
#include "irig106lock.h"
#include "test_support.h"

using namespace tmdq_test;
using ch10setup::Failure;
using ch10setup::PacketRead;

namespace {

/// A recording opened for a test, and the state a setup fills; closes the handle and frees the TMATS when it goes.
struct Setup
{
    int                       handle = -1;
    bool                      open = false;
    Irig106::EnI106Status     status = Irig106::I106_OK;
    Irig106::SuI106Ch10Header header{};
    std::vector<std::uint8_t> buffer;
    Irig106::SuTmatsInfo      tmats{};

    Setup() = default;
    Setup(const Setup&) = delete;
    Setup& operator=(const Setup&) = delete;

    ~Setup()
    {
        const auto lock = lockIrig106Setup();
        if (open)
        {
            Irig106::enI106Ch10Close(handle);
        }
        Irig106::enI106_Free_TmatsInfo(&tmats);
    }

    /// Opens @p path; remembers that a handle is open for every answer but a failed open.
    Failure openRecording(const fs::path& path)
    {
        const auto lock = lockIrig106Setup();
        const Failure failure = ch10setup::openRecording(lock, path, handle, status);
        open = (failure != Failure::Open);
        return failure;
    }

    Failure readTmats()
    {
        const auto lock = lockIrig106Setup();
        return ch10setup::readTmats(lock, handle, header, buffer, tmats, status);
    }
};

} // namespace

TEST_CASE("Ch10Setup: every failure has words of its own, and no failure has none")
{
    std::set<std::string> seen;
    for (const Failure f : {Failure::Open, Failure::TimeSync, Failure::FirstHeader, Failure::NotTmats, Failure::Memory,
                            Failure::ReadTmats, Failure::TmatsLength, Failure::DecodeTmats})
    {
        const std::string words = ch10setup::describe(f);
        CHECK(!words.empty());
        CHECK(seen.insert(words).second);
    }
    CHECK(std::string(ch10setup::describe(Failure::None)).empty());
}

TEST_CASE("Ch10Setup: a recording opens, syncs time, and its first packet is read and decoded")
{
    Setup s;
    REQUIRE(s.openRecording(recordingFile("step_cal, example.ch10")) == Failure::None);
    REQUIRE(s.readTmats() == Failure::None);
    CHECK(s.header.ubyDataType == I106CH10_DTYPE_TMATS);
    CHECK(s.buffer.size() >= s.header.ulPacketLen);
    CHECK(s.tmats.psuFirstRRecord != nullptr);
    CHECK(s.tmats.psuFirstGRecord != nullptr);
}

TEST_CASE("Ch10Setup: a file that will not open is Open, and a file cut inside its first header is TimeSync")
{
    const std::string all = readBytes(recordingFile("step_cal, example.ch10"));
    TempDir dir;

    SUBCASE("missing")
    {
        Setup s;
        CHECK(s.openRecording(dir.file("absent.ch10")) == Failure::Open);
        CHECK(!s.open);
    }
    SUBCASE("text")
    {
        const fs::path path = dir.file("text.ch10");
        writeBytes(path, "this is not a recording\n");
        Setup s;
        CHECK(s.openRecording(path) == Failure::Open);
    }
    SUBCASE("empty")
    {
        const fs::path path = dir.file("empty.ch10");
        writeBytes(path, "");
        Setup s;
        CHECK(s.openRecording(path) == Failure::Open);
    }
    SUBCASE("cut inside the first header: irig106 only warns, so the file opens and time cannot be established")
    {
        const fs::path path = dir.file("short.ch10");
        writeBytes(path, all.substr(0, 10));
        Setup s;
        CHECK(s.openRecording(path) == Failure::TimeSync);
        CHECK(s.open);   // a handle was opened and is the caller's to close
    }
}

TEST_CASE("Ch10Setup: a file whose first packet is not TMATS opens, and the next step says what is wrong")
{
    // irig106 answers "open warning" for it, and opens the file; refusing the warning would report every such file as
    // unopenable instead of saying that it has no TMATS first.
    const std::string all = readBytes(recordingFile("step_cal, example.ch10"));
    const std::uint32_t first = firstPacketLength(all);
    REQUIRE((first > 200 && first < all.size()));
    TempDir dir;
    const fs::path path = dir.file("no-tmats-first.ch10");
    writeBytes(path, all.substr(first));

    Setup s;
    CHECK(s.openRecording(path) == Failure::None);
    CHECK(s.readTmats() == Failure::NotTmats);
    CHECK(s.tmats.psuFirstRRecord == nullptr);   // nothing was decoded
}

TEST_CASE("Ch10Setup: a file with a header and nothing after it has no first packet to read")
{
    const std::string all = readBytes(recordingFile("step_cal, example.ch10"));
    const std::uint32_t first = firstPacketLength(all);
    TempDir dir;
    const fs::path path = dir.file("only-tmats.ch10");
    writeBytes(path, all.substr(0, first));

    // The time-sync step reads on past the TMATS packet to find a time packet, and there is none.
    Setup s;
    CHECK(s.openRecording(path) == Failure::TimeSync);
}

TEST_CASE("Ch10Setup: a packet longer than a buffer may grow to is refused, not allocated")
{
    Irig106::SuI106Ch10Header header{};
    header.ulPacketLen = static_cast<std::uint32_t>(PCMConstants::kMaxPacketBufferSize) + 1;
    std::vector<std::uint8_t> buffer(10);
    Irig106::EnI106Status status = Irig106::I106_OK;

    // No handle is given (-1): the refusal comes before the read, so there is nothing for it to touch.
    CHECK(ch10setup::readPacketData(-1, header, 0, buffer, status) == PacketRead::Memory);
    CHECK(buffer.size() == 10);
    CHECK(status == Irig106::I106_OK);
}

TEST_CASE("Ch10Setup: a packet that cannot be read is a read error, and one that can is read whole")
{
    Setup s;
    REQUIRE(s.openRecording(recordingFile("step_cal, example.ch10")) == Failure::None);
    {
        const auto lock = lockIrig106Setup();
        REQUIRE(Irig106::enI106Ch10ReadNextHeader(s.handle, &s.header) == Irig106::I106_OK);
    }
    s.buffer.assign(4, 0);   // too small: it has to grow to the packet
    CHECK(ch10setup::readPacketData(s.handle, s.header, 0, s.buffer, s.status) == PacketRead::Ok);
    CHECK(s.buffer.size() >= s.header.ulPacketLen);

    // The data of that packet is read; asking again, with no header read in between, is not a packet.
    CHECK(ch10setup::readPacketData(s.handle, s.header, 0, s.buffer, s.status) == PacketRead::ReadError);
}

TEST_CASE("Ch10Setup: a packet's body is what follows its header, the secondary header included")
{
    Irig106::SuI106Ch10Header header{};
    header.ulPacketLen = 100;
    CHECK(ch10setup::bodyLength(header) == 76);
    header.ubyPacketFlags = I106CH10_PFLAGS_SEC_HEADER;
    CHECK(ch10setup::bodyLength(header) == 64);
    header.ulPacketLen = 30;   // shorter than its own two headers: nothing follows them
    CHECK(ch10setup::bodyLength(header) == 0);
}

TEST_CASE("Ch10Setup: a data length that does not fit its packet is refused before anything is read")
{
    // irig106 reads the packet length's worth and checks the data length against nothing; the decoders and the reader's
    // copy of a PCM packet then trust the data length. No handle is given (-1): a refusal comes before the read, and a
    // header that passes goes on to the read, which a handle of -1 fails.
    struct Row { const char* name; std::uint32_t packet; std::uint8_t flags; std::uint32_t data; std::uint32_t floor; PacketRead result; };
    const Row rows[] = {
        {"all of the body",                    100, 0,                          76, 0, PacketRead::ReadError},
        {"one byte past the body",             100, 0,                          77, 0, PacketRead::BadLength},
        {"the whole packet",                   100, 0,                         100, 0, PacketRead::BadLength},
        {"four billion",                       100, 0,                  0xFFFFFFF0u, 0, PacketRead::BadLength},
        {"past a secondary header's body",     100, I106CH10_PFLAGS_SEC_HEADER, 65, 0, PacketRead::BadLength},
        {"all of a secondary header's body",   100, I106CH10_PFLAGS_SEC_HEADER, 64, 0, PacketRead::ReadError},
        {"a packet shorter than its header",    20, 0,                           0, 0, PacketRead::BadLength},
        {"below the type's floor",             100, 0,                           3, 4, PacketRead::BadLength},
        {"at the type's floor",                100, 0,                           4, 4, PacketRead::ReadError},
        {"none, and no floor",                 100, 0,                           0, 0, PacketRead::ReadError},
    };
    for (const Row& row : rows)
    {
        INFO(row.name);
        Irig106::SuI106Ch10Header header{};
        header.ulPacketLen = row.packet;
        header.ubyPacketFlags = row.flags;
        header.ulDataLen = row.data;
        std::vector<std::uint8_t> buffer(10);
        Irig106::EnI106Status status = Irig106::I106_OK;
        CHECK(ch10setup::readPacketData(-1, header, row.floor, buffer, status) == row.result);
        if (row.result == PacketRead::BadLength)
        {
            CHECK(buffer.size() == 10);   // nothing allocated for it
            CHECK(status == Irig106::I106_OK);
        }
    }
}

TEST_CASE("Ch10Setup: a TMATS packet whose data length does not fit it is refused, not decoded")
{
    // The TMATS decoder reads ulDataLen - 4 bytes of text from the buffer: past the packet, or, under four, four billion.
    const std::string all = readBytes(recordingFile("step_cal, example.ch10"));
    const std::uint32_t first = firstPacketLength(all);
    REQUIRE(first > 200);
    const std::uint32_t body = first - 24;   // step_cal's TMATS packet has no secondary header

    struct Row { const char* name; std::uint32_t data; Failure failure; };
    const Row rows[] = {
        {"one byte past the packet", body + 1, Failure::TmatsLength},
        {"far past the packet",      body + 100000, Failure::TmatsLength},
        {"shorter than its channel-specific word", 2, Failure::TmatsLength},
        {"all of the packet",        body, Failure::None},
    };
    for (const Row& row : rows)
    {
        INFO(row.name);
        std::string bytes = all;
        setPacketDataLength(bytes, 0, row.data);
        TempDir dir;
        const fs::path path = dir.file("corrupt-tmats.ch10");
        writeBytes(path, bytes);

        Setup s;
        REQUIRE(s.openRecording(path) == Failure::None);
        CHECK(s.readTmats() == row.failure);
        if (row.failure != Failure::None)
        {
            CHECK(s.tmats.psuFirstRRecord == nullptr);   // nothing was decoded
        }
    }
}