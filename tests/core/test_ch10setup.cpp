// ch10setup: the steps both Chapter 10 readers start a recording with, tested directly (follow-up audit B9 and S3).
//
// The two readers used to write these steps out each, and had come to differ about what irig106 only warns about, about
// how big a packet may be, and about a TMATS that would not read. Here each step is given a file that fails it, and the
// reader tests hold the two readers to the same answer.

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "doctest.h"
#include "ch10setup.h"
#include "constants.h"
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
                            Failure::ReadTmats, Failure::DecodeTmats})
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
    CHECK(ch10setup::readPacketData(-1, header, buffer, status) == PacketRead::Memory);
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
    CHECK(ch10setup::readPacketData(s.handle, s.header, s.buffer, s.status) == PacketRead::Ok);
    CHECK(s.buffer.size() >= s.header.ulPacketLen);

    // The data of that packet is read; asking again, with no header read in between, is not a packet.
    CHECK(ch10setup::readPacketData(s.handle, s.header, s.buffer, s.status) == PacketRead::ReadError);
}
