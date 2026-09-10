/**
 * @file framesetup.cpp
 * @brief Implementation of FrameSetup — TOML-based frame parameter loading.
 */

#include "framesetup.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include "constants.h"
#include "tomlconfighelper.h"

const QStringList FrameSetup::kSettingsGroups = {
    // Non-parameter metadata sections, skipped by tryLoadingFile(). "EMPTY"
    // lets a word map explicitly list an unused/placeholder word (e.g. the
    // unpopulated 4th channel of a 4-channel receiver card) so its Word number
    // is not silently skipped in the file, without turning it into a plotted
    // channel. Repeated [EMPTY] sections are fine — they are all skipped here.
    "Defaults", "Frame", "Parameters", "Time", "Receivers", "Bounds", "EMPTY"
};

QStringList FrameSetup::readGroupsInFileOrder(const QString& filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return {};
    }

    static const QRegularExpression sectionPattern(R"(^\[(.+)\]\s*$)");
    QStringList groups;
    QTextStream in(&file);

    while (!in.atEnd())
    {
        QString line = in.readLine().trimmed();
        QRegularExpressionMatch match = sectionPattern.match(line);
        if (match.hasMatch())
        {
            groups.append(match.captured(1));
        }
    }

    return groups;
}

FrameSetup::FrameSetup(QObject* parent) :
    QObject(parent)
{

}

bool FrameSetup::tryLoadingFile(const QString& filename, int num_words_in_minor_frame)
{
    clearParameters();

    QSettings settings(filename, TomlConfigHelper::format());
    if (settings.status() != QSettings::NoError)
    {
        return false;
    }

    QStringList groups = readGroupsInFileOrder(filename);
    if (groups.isEmpty())
    {
        return false;
    }

    for (const QString& group : groups)
    {
        if (kSettingsGroups.contains(group))
        {
            continue;
        }

        settings.beginGroup(group);

        if (!settings.contains("Word"))
        {
            settings.endGroup();
            return false;
        }

        bool word_ok = false;
        int parameter_word = settings.value("Word").toInt(&word_ok) - 1;

        // num_words_in_minor_frame counts the sync word, so the last addressable
        // DATA word is index num_words_in_minor_frame - 2 (the final slot is the
        // sync word's, never populated as a parameter). Hence the "- 1" upper bound
        // here. This intentionally rejects Word == num_words_in_minor_frame; see the
        // tryLoadingFileWordEqualsFrameSize / ...RejectsBoundary tests.
        if (!word_ok || parameter_word < 0 || parameter_word >= num_words_in_minor_frame - 1)
        {
            settings.endGroup();
            return false;
        }

        addParameter(group, parameter_word);

        settings.endGroup();
    }

    return true;
}

int FrameSetup::length() const
{
    return static_cast<int>(m_parameters.size());
}

const ParameterInfo* FrameSetup::getParameter(int i) const
{
    if (i < 0 || i >= m_parameters.size())
    {
        return nullptr;
    }
    return &(m_parameters[i]);
}

ParameterInfo* FrameSetup::getParameter(int i)
{
    if (i < 0 || i >= m_parameters.size())
    {
        return nullptr;
    }
    return &(m_parameters[i]);
}

void FrameSetup::clearParameters()
{
    m_parameters.clear();
}

void FrameSetup::addParameter(const QString& name, int word)
{
    ParameterInfo parameter;
    parameter.name = name;
    parameter.word = word;
    parameter.is_enabled = true; // slope/scale/sample_sum default to 0 via NSDMIs

    m_parameters.append(parameter);
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

int FrameSetup::attachCalibrationProfiles(const QHash<int, CalibrationProfile>& by_word)
{
    int attached = 0;
    for (ParameterInfo& param : m_parameters)
    {
        const auto it = by_word.constFind(param.word);
        if (it != by_word.constEnd() && it.value().valid)
        {
            param.profile = it.value();
            attached++;
        }
    }
    return attached;
}

QString FrameSetup::channelPrefix(int channel_index)
{
    if (channel_index < UIConstants::kNumKnownPrefixes)
    {
        return UIConstants::kChannelPrefixes[channel_index];
    }
    return "CH" + QString::number(channel_index + 1);
}

QString FrameSetup::receiverParameterName(int channel_index, int receiver_index)
{
    return channelPrefix(channel_index) + "_RCVR" + QString::number(receiver_index + 1);
}

namespace {
    /// Shared split point for the two accessors below, so the token and the
    /// "trailing digits" rule are stated once.
    constexpr QLatin1String kReceiverToken("_RCVR");
}

int FrameSetup::receiverIndexFromName(const QString& name)
{
    const int at = name.lastIndexOf(kReceiverToken);
    if (at < 0)
    {
        return 0;
    }
    bool ok = false;
    const int receiver = name.mid(at + kReceiverToken.size()).toInt(&ok);
    // A name ending in "_RCVR" with no number, or with trailing non-digits, is
    // not a receiver name — treat it as unparseable rather than guessing.
    return (ok && receiver > 0) ? receiver : 0;
}

QString FrameSetup::channelPartOfName(const QString& name)
{
    const int at = name.lastIndexOf(kReceiverToken);
    return (at > 0 && receiverIndexFromName(name) > 0) ? name.left(at) : name;
}

bool FrameSetup::buildDefaultReceiverMap(int num_receivers, int receiver_channels,
                                         int num_words_in_minor_frame, QString& error)
{
    const int total_params = num_receivers * receiver_channels;
    if (total_params <= 0 || total_params >= num_words_in_minor_frame)
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

void FrameSetup::saveToSettings(QSettings& settings)
{
    for (const auto& param : m_parameters)
    {
        settings.beginGroup(param.name);
        settings.setValue("Word", param.word + 1);
        settings.endGroup();
    }
}
