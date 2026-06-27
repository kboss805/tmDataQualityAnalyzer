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

        ParameterInfo parameter = ParameterInfo();
        parameter.name = group;
        parameter.word = parameter_word;
        parameter.slope = 0;
        parameter.scale = 0;
        parameter.is_enabled = true;
        parameter.sample_sum = 0;

        m_parameters.append(parameter);

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
    ParameterInfo parameter = ParameterInfo();
    parameter.name = name;
    parameter.word = word;
    parameter.slope = 0;
    parameter.scale = 0;
    parameter.is_enabled = true;
    parameter.sample_sum = 0;

    m_parameters.append(parameter);
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
