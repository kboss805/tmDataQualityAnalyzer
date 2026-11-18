// The naming rules of a receiver parameter, "<channel>_RCVR<N>": the prefix for a channel position, the name made from a
// channel and a receiver, and the number and the channel part read back from a name (ported in plan step 2.4 with the
// FrameSetup suite they came out of; the series builder and the CSV column schema read names with the same rules).

#include <string>

#include "doctest.h"
#include "receivernames.h"

TEST_CASE("ReceiverNames: channelPrefix names known positions, then falls back")
{
    // The known channel positions, then the CH<n+1> fallback past them. receivernames is where the naming lives, and is
    // what the calibration extractor shares with the main run.
    CHECK(receivernames::channelPrefix(0) == "L");
    CHECK(receivernames::channelPrefix(1) == "R");
    CHECK(receivernames::channelPrefix(2) == "C");

    // First index past the known prefixes, and well past them.
    CHECK(receivernames::channelPrefix(3) == "CH4");
    CHECK(receivernames::channelPrefix(10) == "CH11");
    CHECK(receivernames::channelPrefix(99) == "CH100");
    CHECK(receivernames::channelPrefix(999) == "CH1000");
    CHECK(receivernames::channelPrefix(9999) == "CH10000");
}

TEST_CASE("ReceiverNames: receiverParameterName combines prefix and receiver")
{
    CHECK(receivernames::receiverParameterName(0, 0) == "L_RCVR1");
    CHECK(receivernames::receiverParameterName(1, 2) == "R_RCVR3");
    CHECK(receivernames::receiverParameterName(2, 15) == "C_RCVR16");

    // A channel index past the known prefixes carries the fallback through.
    CHECK(receivernames::receiverParameterName(3, 0) == "CH4_RCVR1");
    CHECK(receivernames::receiverParameterName(5, 4) == "CH6_RCVR5");
}

TEST_CASE("ReceiverNames: receiverIndexFromName inverts receiverParameterName")
{
    // Round-trips against the constructor it inverts, across the known channel prefixes and past them (where the
    // prefix becomes "CH<n+1>").
    for (int receiver = 0; receiver < 16; receiver++)
    {
        for (int channel = 0; channel < 5; channel++)
        {
            const std::string name = receivernames::receiverParameterName(channel, receiver);
            INFO(name);
            CHECK(receivernames::receiverIndexFromName(name) == receiver + 1);
            CHECK(receivernames::channelPartOfName(name) == receivernames::channelPrefix(channel));
        }
    }

    // A receiver-params TOML may name parameters anything at all. Those must read as "no receiver number" (0) rather
    // than a guess, so the calibration summary falls back to listing names instead of inventing receivers.
    CHECK(receivernames::receiverIndexFromName("AGC_LEFT") == 0);
    CHECK(receivernames::receiverIndexFromName("") == 0);
    CHECK(receivernames::receiverIndexFromName("L_RCVR") == 0);   // token, no number
    CHECK(receivernames::receiverIndexFromName("L_RCVR0") == 0);  // receivers are 1-based
    CHECK(receivernames::receiverIndexFromName("L_RCVRx2") == 0); // not a bare number
    // An unparseable name keeps its whole self as the "channel part".
    CHECK(receivernames::channelPartOfName("AGC_LEFT") == "AGC_LEFT");
    CHECK(receivernames::channelPartOfName("L_RCVR0") == "L_RCVR0");   // receiver 0 is no receiver, so no split either
    CHECK(receivernames::channelPartOfName("") == "");
}

TEST_CASE("ReceiverNames: receiverNumberInName is receiverIndexFromName with a negative number kept")
{
    CHECK(receivernames::receiverNumberInName("L_RCVR-1") == -1);
    CHECK(receivernames::receiverIndexFromName("L_RCVR-1") == 0);
    CHECK(receivernames::receiverNumberInName("L_RCVR0") == 0);
    CHECK(receivernames::receiverNumberInName("L_RCVR 3") == 3);
    CHECK(receivernames::receiverNumberInName("L_RCVR3x") == 0);
    CHECK(receivernames::receiverNumberInName("A_RCVR1_RCVR2") == 2);
    CHECK(receivernames::receiverNumberInName("AGC_LEFT") == 0);
    CHECK(receivernames::receiverNumberInName("") == 0);
}

TEST_CASE("ReceiverNames: a receiver number in a name is a whole decimal number, or none")
{
    // The text after "_RCVR" is read by textparse::parseInteger: base 10, the whole text digits after one optional
    // sign, surrounding whitespace ignored. These answers were pinned FIRST, against v2.14.2's implementation, before
    // the helper moved to std::string, because a plain from_chars would change every one of them.
    // The token is the LAST "_RCVR": an earlier one is just part of the name.
    CHECK(receivernames::receiverIndexFromName("A_RCVR1_RCVR2") == 2);
    CHECK(receivernames::channelPartOfName("A_RCVR1_RCVR2") == "A_RCVR1");
    // Surrounding whitespace is ignored and a leading '+' accepted...
    CHECK(receivernames::receiverIndexFromName("L_RCVR 3") == 3);
    CHECK(receivernames::receiverIndexFromName("L_RCVR3 ") == 3);
    CHECK(receivernames::receiverIndexFromName("L_RCVR+4") == 4);
    // ...but not a second sign, a negative number, trailing junk or an overflow.
    CHECK(receivernames::receiverIndexFromName("L_RCVR++4") == 0);
    CHECK(receivernames::receiverIndexFromName("L_RCVR-1") == 0);
    CHECK(receivernames::receiverIndexFromName("L_RCVR3x") == 0);
    CHECK(receivernames::receiverIndexFromName("L_RCVR99999999999") == 0);
    // A name that STARTS with the token has no channel part to split off: the whole name comes back, even though the
    // receiver number parses.
    CHECK(receivernames::receiverIndexFromName("_RCVR2") == 2);
    CHECK(receivernames::channelPartOfName("_RCVR2") == "_RCVR2");
    // The channel part is everything before the token, whatever it holds.
    CHECK(receivernames::channelPartOfName("CH12_RCVR7") == "CH12");
    CHECK(receivernames::channelPartOfName("L_RCVR") == "L_RCVR");
    // Non-ASCII names pass through untouched.
    const std::string alpha = "\xCE\x91";   // U+0391, Greek capital alpha
    CHECK(receivernames::receiverIndexFromName(alpha + "_RCVR5") == 5);
    CHECK(receivernames::channelPartOfName(alpha + "_RCVR5") == alpha);
}
