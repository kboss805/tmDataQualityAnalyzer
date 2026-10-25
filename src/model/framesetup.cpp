/**
 * @file framesetup.cpp
 * @brief Implementation of FrameSetup — TOML-based frame parameter loading.
 */

#include "framesetup.h"

#include <algorithm>
#include <string>
#include <vector>

#include "constants.h"
#include "containerutil.h"
#include "textparse.h"
#include "tomldocument.h"

const std::vector<std::string> FrameSetup::kSettingsGroups = {
    // Non-parameter metadata sections, skipped by tryLoadingFile(). "EMPTY"
    // lets a word map explicitly list an unused/placeholder word (e.g. the
    // unpopulated 4th channel of a 4-channel receiver card) so its Word number
    // is not silently skipped in the file, without turning it into a plotted
    // channel. Repeated [EMPTY] sections are fine — they are all skipped here.
    "Defaults", "Frame", "Parameters", "Time", "Receivers", "Bounds", "EMPTY"
};

bool FrameSetup::tryLoadingFile(const std::filesystem::path& filename, int num_words_in_minor_frame)
{
    clearParameters();

    // One parse, not two. This used to build a QSettings AND separately re-read the
    // file with readGroupsInFileOrder(), because QSettings returns childGroups()
    // sorted while a word map's order is the order the file lists it in.
    const TomlDocument doc = TomlDocument::load(filename);
    if (!doc.ok())
    {
        return false;
    }

    const std::vector<std::string>& groups = doc.sectionsInFileOrder();
    if (groups.empty())
    {
        return false;
    }

    for (const std::string& group : groups)
    {
        if (std::find(kSettingsGroups.begin(), kSettingsGroups.end(), group)
                != kSettingsGroups.end())
        {
            continue;
        }

        const std::string word_key = group + "/Word";
        if (!doc.contains(word_key))
        {
            return false;
        }

        bool word_ok = false;
        int parameter_word = doc.integer(word_key, 0, &word_ok) - 1;

        // num_words_in_minor_frame counts the sync word, so the last addressable
        // DATA word is index num_words_in_minor_frame - 2 (the final slot is the
        // sync word's, never populated as a parameter). Hence the "- 1" upper bound
        // here. This intentionally rejects Word == num_words_in_minor_frame; see the
        // tryLoadingFileWordEqualsFrameSize / ...RejectsBoundary tests.
        if (!word_ok || parameter_word < 0 || parameter_word >= num_words_in_minor_frame - 1)
        {
            return false;
        }

        addParameter(group, parameter_word);
    }

    return true;
}

int FrameSetup::length() const
{
    return static_cast<int>(m_parameters.size());
}

const ParameterInfo* FrameSetup::getParameter(int i) const
{
    if (i < 0 || i >= intSize(m_parameters))
    {
        return nullptr;
    }
    return &(m_parameters[i]);
}

ParameterInfo* FrameSetup::getParameter(int i)
{
    if (i < 0 || i >= intSize(m_parameters))
    {
        return nullptr;
    }
    return &(m_parameters[i]);
}

void FrameSetup::clearParameters()
{
    m_parameters.clear();
}

void FrameSetup::addParameter(const std::string& name, int word)
{
    ParameterInfo parameter;
    parameter.name = name;
    parameter.word = word;
    parameter.is_enabled = true; // slope/scale/sample_sum default to 0 via NSDMIs

    m_parameters.push_back(parameter);
}

void FrameSetup::applyLinearCalibration(int polarity_index, int slope_index, double scale_dB_per_V)
{
    const double voltage_lower = UIConstants::kSlopeVoltageLower[slope_index] * scale_dB_per_V;
    const double voltage_upper = UIConstants::kSlopeVoltageUpper[slope_index] * scale_dB_per_V;
    const bool   negative_polarity = (polarity_index == 1);

    for (ParameterInfo& param : m_parameters)
    {
        param.slope = (voltage_upper - voltage_lower) / PCMConstants::kMaxRawSampleValue;
        if (negative_polarity)
        {
            param.slope *= -1;
            param.scale = -voltage_upper / (voltage_upper - voltage_lower) * PCMConstants::kMaxRawSampleValue;
        }
        else
        {
            param.scale = voltage_lower / (voltage_upper - voltage_lower) * PCMConstants::kMaxRawSampleValue;
        }
        param.is_enabled = true;
        param.sample_sum = 0;
    }
}

int FrameSetup::attachCalibrationProfiles(const std::unordered_map<int, CalibrationProfile>& by_word)
{
    int attached = 0;
    for (ParameterInfo& param : m_parameters)
    {
        const auto it = by_word.find(param.word);
        if (it != by_word.end() && it->second.valid)
        {
            param.profile = it->second;
            attached++;
        }
    }
    return attached;
}

int FrameSetup::wordsInMinorFrame(int bits_in_minor_frame)
{
    return (bits_in_minor_frame + PCMConstants::kCommonWordLen - 1) / PCMConstants::kCommonWordLen;
}

ReceiverParams FrameSetup::readReceiverParams(const std::filesystem::path& path,
                                              const ReceiverParams& defaults)
{
    ReceiverParams params = defaults;
    const TomlDocument cfg = TomlDocument::load(path);

    params.polarityIndex = cfg.integer("Parameters/Polarity", params.polarityIndex);
    params.slopeIndex    = cfg.integer("Parameters/Slope", params.slopeIndex);

    const double scale = cfg.number("Parameters/Scale", params.scaleDdBPerV);
    if (scale > 0.0)
    {
        params.scaleDdBPerV = scale;
    }

    // The receiver counts live under [Receivers] in every shipped parameters file.
    // Files the dialog itself wrote before v2.12.1 put them under [Parameters]
    // instead, so read those too rather than silently showing the defaults. Spelled
    // as an explicit contains() rather than a nested default, which is what the
    // QSettings version had to do and which read as though [Receivers] were optional
    // in a different sense than it is.
    const int receivers = cfg.contains("Receivers/Count")
        ? cfg.integer("Receivers/Count", params.numReceivers)
        : cfg.integer("Parameters/NumReceivers", params.numReceivers);
    const int channels  = cfg.contains("Receivers/ChannelsPerReceiver")
        ? cfg.integer("Receivers/ChannelsPerReceiver", params.receiverChannels)
        : cfg.integer("Parameters/ReceiverChannels", params.receiverChannels);
    if (receivers > 0)
    {
        params.numReceivers = receivers;
    }
    if (channels > 0)
    {
        params.receiverChannels = channels;
    }
    return params;
}

bool FrameSetup::writeReceiverParams(const std::filesystem::path& path, const ReceiverParams& params) const
{
    // A file with no word map parses but yields no parameters, and every consumer
    // then rejects it. Refusing here is what keeps save and load inverses.
    if (m_parameters.empty())
    {
        return false;
    }

    // No QFile::remove() first any more: TomlDocument::save() truncates. QSettings
    // merged into whatever the file already held, so without the delete an overwrite
    // left the old file's word map behind alongside the new one.
    TomlDocument cfg;
    cfg.setValue("Parameters/Polarity", params.polarityIndex);
    cfg.setValue("Parameters/Slope",    params.slopeIndex);
    cfg.setValue("Parameters/Scale",    params.scaleDdBPerV);
    cfg.setValue("Receivers/Count",               params.numReceivers);
    cfg.setValue("Receivers/ChannelsPerReceiver", params.receiverChannels);
    writeWordMap(cfg);
    return cfg.save(path);
}

bool FrameSetup::saveReceiverParamsFile(const std::filesystem::path& path,
                                        const ReceiverParams& params,
                                        const std::filesystem::path& word_map_source,
                                        int num_words_in_minor_frame,
                                        std::string& error)
{
    FrameSetup setup;
    bool have_map = false;
    std::error_code ec; // exists() with an error code never throws on a bad path
    if (!word_map_source.empty() && std::filesystem::exists(word_map_source, ec))
    {
        have_map = setup.tryLoadingFile(word_map_source, num_words_in_minor_frame)
                   && setup.length() > 0;
    }
    if (!have_map)
    {
        // A rejected load can leave a partial map behind, and buildDefaultReceiverMap
        // appends — clear first so the default map is the whole file.
        setup.clearParameters();
        if (!setup.buildDefaultReceiverMap(params.numReceivers, params.receiverChannels,
                                           num_words_in_minor_frame, error))
        {
            return false;
        }
    }
    if (!setup.writeReceiverParams(path, params))
    {
        const auto name = path.filename().u8string();
        error = "Could not write '" + std::string(name.begin(), name.end()) + "'.";
        return false;
    }
    return true;
}

std::string FrameSetup::channelPrefix(int channel_index)
{
    if (channel_index < UIConstants::kNumKnownPrefixes)
    {
        return UIConstants::kChannelPrefixes[channel_index];
    }
    return "CH" + std::to_string(channel_index + 1);
}

std::string FrameSetup::receiverParameterName(int channel_index, int receiver_index)
{
    return channelPrefix(channel_index) + "_RCVR" + std::to_string(receiver_index + 1);
}

namespace {
    /// Shared split point for the two accessors below, so the token and the
    /// "trailing digits" rule are stated once.
    constexpr const char* kReceiverToken = "_RCVR";
    constexpr std::size_t kReceiverTokenLength = 5;
}

int FrameSetup::receiverIndexFromName(const std::string& name)
{
    const std::size_t at = name.rfind(kReceiverToken);
    if (at == std::string::npos)
    {
        return 0;
    }
    // QString::toInt's rules (whitespace and a leading '+' tolerated) via the shared
    // parser; pinned by receiverNameParsingMatchesQStringToInt.
    int receiver = 0;
    const bool ok = textparse::parseInteger(name.substr(at + kReceiverTokenLength), receiver);
    // A name ending in "_RCVR" with no number, or with trailing non-digits, is
    // not a receiver name — treat it as unparseable rather than guessing.
    return (ok && receiver > 0) ? receiver : 0;
}

std::string FrameSetup::channelPartOfName(const std::string& name)
{
    const std::size_t at = name.rfind(kReceiverToken);
    return (at != std::string::npos && at > 0 && receiverIndexFromName(name) > 0)
        ? name.substr(0, at)
        : name;
}

bool FrameSetup::buildDefaultReceiverMap(int num_receivers, int receiver_channels,
                                         int num_words_in_minor_frame, std::string& error)
{
    // Each count on its own as well as the product: two negative counts make a positive product, and the loops below
    // would then build nothing and report success.
    const int total_params = num_receivers * receiver_channels;
    if (num_receivers <= 0 || receiver_channels <= 0 || total_params <= 0
        || total_params >= num_words_in_minor_frame)
    {
        error = "Num Receivers x Receiver Channels exceeds the words available "
                "in the minor frame.";
        return false;
    }
    for (int r = 0; r < num_receivers; r++)
    {
        for (int c = 0; c < receiver_channels; c++)
        {
            addParameter(receiverParameterName(c, r), r * receiver_channels + c);
        }
    }
    return true;
}

void FrameSetup::writeWordMap(TomlDocument& doc) const
{
    for (const auto& param : m_parameters)
    {
        doc.setValue(param.name + "/Word", param.word + 1);
    }
}
