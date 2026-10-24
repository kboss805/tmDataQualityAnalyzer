// textfile: how a settings or step file's bytes become UTF-8 text, and how a path's name is spelled in a message.
// No Qt. The TomlDocument tests reach this only through a file or two; these cases hold it directly, because
// every encoding rule below is something a port off QString would change without any caller noticing.

#include <cstdint>
#include <string>
#include <vector>

#include "doctest.h"
#include "test_support.h"
#include "textfile.h"

using namespace tmdq_test;

namespace {

/// UTF-16 bytes for these code units, with a byte-order mark.
std::string utf16(const std::vector<char16_t>& units, bool bigEndian, bool bom = true)
{
    std::string out;
    if (bom)
    {
        out = bigEndian ? std::string("\xFE\xFF", 2) : std::string("\xFF\xFE", 2);
    }
    for (const char16_t u : units)
    {
        const char hi = static_cast<char>(u >> 8);
        const char lo = static_cast<char>(u & 0xFF);
        out.push_back(bigEndian ? hi : lo);
        out.push_back(bigEndian ? lo : hi);
    }
    return out;
}

/// What decoding one code point (as UTF-16 units, either byte order) gives.
void checkDecodes(const std::vector<char16_t>& units, const std::string& expected)
{
    for (const bool bigEndian : {false, true})
    {
        INFO("big endian: " << bigEndian);
        CHECK(textfile::decodeText(utf16(units, bigEndian)) == expected);
    }
}

const std::string kReplacement = "\xEF\xBF\xBD";   // U+FFFD

} // namespace

TEST_CASE("textfile: a UTF-8 byte-order mark is dropped, and a bare one leaves nothing")
{
    CHECK(textfile::decodeText("\xEF\xBB\xBF" "abc") == "abc");
    CHECK(textfile::decodeText("\xEF\xBB\xBF") == "");          // exactly the mark
    CHECK(textfile::decodeText("\xEF\xBB") == "\xEF\xBB");     // two bytes of it are not a mark
    CHECK(textfile::decodeText("abc") == "abc");
    CHECK(textfile::decodeText("") == "");
    CHECK(textfile::decodeText("\xEF\xBB\xBE" "x") == "\xEF\xBB\xBE" "x");   // each of the three bytes matters
    CHECK(textfile::decodeText("\xEF\xBA\xBF" "x") == "\xEF\xBA\xBF" "x");
    CHECK(textfile::decodeText("\xEE\xBB\xBF" "x") == "\xEE\xBB\xBF" "x");
}

TEST_CASE("textfile: UTF-16 is recognised by its mark in either byte order")
{
    CHECK(textfile::decodeText(utf16({u'a', u'b'}, false)) == "ab");
    CHECK(textfile::decodeText(utf16({u'a', u'b'}, true)) == "ab");
    CHECK(textfile::decodeText(utf16({}, false)) == "");        // just the mark
    CHECK(textfile::decodeText(utf16({}, true)) == "");

    // Without a mark it is not UTF-16: the bytes are taken as they are.
    CHECK(textfile::decodeText(utf16({u'a'}, false, false)) == std::string("a\0", 2));

    // A mark that is nearly one is not one.
    CHECK(textfile::decodeText(std::string("\xFF\xFD" "a\0", 4)) == std::string("\xFF\xFD" "a\0", 4));
    CHECK(textfile::decodeText(std::string("\xFE\xFE" "\0a", 4)) == std::string("\xFE\xFE" "\0a", 4));
}

TEST_CASE("textfile: each length of UTF-8 starts and ends where it should")
{
    checkDecodes({0x007F}, "\x7F");
    checkDecodes({0x0080}, "\xC2\x80");                   // the shortest two-byte
    checkDecodes({0x00E9}, "\xC3\xA9");
    checkDecodes({0x07FF}, "\xDF\xBF");                   // the longest two-byte
    checkDecodes({0x0800}, "\xE0\xA0\x80");               // the shortest three-byte
    checkDecodes({0x20AC}, "\xE2\x82\xAC");
    checkDecodes({0xD7FF}, "\xED\x9F\xBF");               // the last before the surrogates
    checkDecodes({0xE000}, "\xEE\x80\x80");               // the first after them
    checkDecodes({0xFFFF}, "\xEF\xBF\xBF");               // the longest three-byte
}

TEST_CASE("textfile: a surrogate pair is one four-byte character")
{
    checkDecodes({0xD800, 0xDC00}, "\xF0\x90\x80\x80");   // U+10000, the first
    checkDecodes({0xD83D, 0xDE00}, "\xF0\x9F\x98\x80");   // U+1F600
    checkDecodes({0xDBFF, 0xDFFF}, "\xF4\x8F\xBF\xBF");   // U+10FFFF, the last
    checkDecodes({u'a', 0xD83D, 0xDE00, u'b'}, "a\xF0\x9F\x98\x80" "b");   // among other text
}

TEST_CASE("textfile: a surrogate with no partner becomes the replacement character")
{
    checkDecodes({0xD83D}, kReplacement);                          // a high one at the very end
    checkDecodes({0xDE00}, kReplacement);                          // a low one alone
    checkDecodes({0xD83D, u'A'}, kReplacement + "A");              // a high one followed by something else
    checkDecodes({0xD83D, 0xD83D, 0xDE00}, kReplacement + "\xF0\x9F\x98\x80");   // a high one, then a whole pair
    checkDecodes({0xD83D, 0xE000}, kReplacement + "\xEE\x80\x80"); // the next unit is above the low range
    checkDecodes({0xDE00, 0xD83D, 0xDE00}, kReplacement + "\xF0\x9F\x98\x80");   // a low one, then a whole pair
}

TEST_CASE("textfile: an odd trailing byte of UTF-16 is not a character")
{
    std::string bytes = utf16({u'A'}, false);
    bytes.push_back('B');
    CHECK(textfile::decodeText(bytes) == "A");
    bytes = utf16({u'A'}, true);
    bytes.push_back('B');
    CHECK(textfile::decodeText(bytes) == "A");
}

TEST_CASE("textfile: readText returns the bytes exactly, decoded")
{
    TempDir tmp;
    std::string text;

    // Binary, not text: a carriage return and a Ctrl-Z (end of file to a text-mode reader) come through.
    const std::string raw = "A=1\r\nB=2\x1A" "C=3\r\n";
    writeBytes(tmp.file("raw.toml"), raw);
    REQUIRE(textfile::readText(tmp.file("raw.toml"), text));
    CHECK(text == raw);

    writeBytes(tmp.file("bom.toml"), "\xEF\xBB\xBF" "K=1\n");
    REQUIRE(textfile::readText(tmp.file("bom.toml"), text));
    CHECK(text == "K=1\n");

    writeBytes(tmp.file("utf16.toml"), utf16({u'K', u'=', u'1'}, false));
    REQUIRE(textfile::readText(tmp.file("utf16.toml"), text));
    CHECK(text == "K=1");

    writeBytes(tmp.file("empty.toml"), "");
    text = "left over";
    REQUIRE(textfile::readText(tmp.file("empty.toml"), text));
    CHECK(text.empty());

    text = "untouched";
    CHECK(!textfile::readText(tmp.file("absent.toml"), text));
    CHECK(text == "untouched");
}

TEST_CASE("textfile: a file name for a message is the name alone, in UTF-8")
{
    const fs::path path = fs::path("some") / "folder" / std::u8string(u8"naïve-Ω.toml");
    CHECK(textfile::utf8FileName(path) == utf8(u8"naïve-Ω.toml"));
    CHECK(textfile::utf8FileName(fs::path("plain.toml")) == "plain.toml");
    CHECK(textfile::utf8FileName(fs::path("a") / "b.toml") == "b.toml");
}

TEST_CASE("textfile: a UTF-8 string is a path whatever the code page")
{
    // The plain std::string constructor would read these bytes in the ANSI code page.
    const std::string bytes = utf8(u8"omega-Ωé");
    const fs::path path = textfile::pathFromUtf8(bytes);
    const std::u8string back = path.u8string();
    CHECK(std::string(back.begin(), back.end()) == bytes);
    CHECK(textfile::pathFromUtf8("plain/dir").generic_string() == "plain/dir");

    TempDir tmp;
    const fs::path file = tmp.path() / textfile::pathFromUtf8(utf8(u8"café-Ω.toml"));
    writeBytes(file, "K=1\n");
    CHECK(fs::exists(file));
}
