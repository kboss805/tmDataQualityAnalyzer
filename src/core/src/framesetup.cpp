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
#include "receivernames.h"
#include "tomldocument.h"

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): a handful of short strings; failing to allocate them at start is fatal anyway
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

    // One parse: TomlDocument keeps the sections in file order, and a word map's
    // order is the order the file lists it in.
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
        // "tryLoadingFile with word equal to the frame size fails" and "tryLoadingFile with a
        // smaller frame rejects the boundary" test cases.
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
    // slope/scale default to 0 via NSDMIs

    m_parameters.push_back(parameter);
}

void FrameSetup::applyLinearCalibration(int polarity_index, int slope_index, double scale_dB_per_V)
{
    const double voltage_lower = ReceiverConstants::kSlopeVoltageLower[slope_index] * scale_dB_per_V;
    const double voltage_upper = ReceiverConstants::kSlopeVoltageUpper[slope_index] * scale_dB_per_V;
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
            addParameter(receivernames::receiverParameterName(c, r), r * receiver_channels + c);
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
