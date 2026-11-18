/**
 * @file receivernames.cpp
 * @brief Implementation of the naming rules of a receiver parameter.
 */

#include "receivernames.h"

#include <cstddef>
#include <string>

#include "constants.h"
#include "textparse.h"

namespace receivernames {

namespace {
    /// Shared split point for the accessors below, so the token and the
    /// "trailing digits" rule are stated once.
    constexpr const char* kReceiverToken = "_RCVR";
    constexpr std::size_t kReceiverTokenLength = 5;
}

std::string channelPrefix(int channel_index)
{
    if (channel_index < ReceiverConstants::kNumKnownPrefixes)
    {
        return ReceiverConstants::kChannelPrefixes[channel_index];
    }
    return "CH" + std::to_string(channel_index + 1);
}

std::string receiverParameterName(int channel_index, int receiver_index)
{
    return channelPrefix(channel_index) + kReceiverToken + std::to_string(receiver_index + 1);
}

int receiverNumberInName(const std::string& name)
{
    const std::size_t at = name.rfind(kReceiverToken);
    if (at == std::string::npos)
    {
        return 0;
    }
    // Base 10, surrounding whitespace and a leading '+' tolerated, via the shared parser;
    // pinned by "a receiver number in a name is a whole decimal number, or none"
    // (test_receivernames.cpp).
    int receiver = 0;
    const bool ok = textparse::parseInteger(name.substr(at + kReceiverTokenLength), receiver);
    // A name ending in "_RCVR" with no number, or with trailing non-digits, is
    // not a receiver name — treat it as unparseable rather than guessing.
    return ok ? receiver : 0;
}

int receiverIndexFromName(const std::string& name)
{
    const int receiver = receiverNumberInName(name);
    return receiver > 0 ? receiver : 0;
}

std::string channelPartOfName(const std::string& name)
{
    const std::size_t at = name.rfind(kReceiverToken);
    return (at != std::string::npos && at > 0 && receiverIndexFromName(name) > 0)
        ? name.substr(0, at)
        : name;
}

} // namespace receivernames
