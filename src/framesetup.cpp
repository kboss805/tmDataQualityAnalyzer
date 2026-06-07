/**
 * @file framesetup.cpp
 * @brief Implementation of FrameSetup — TOML-based frame parameter loading.
 */

#include "framesetup.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include "tomlconfighelper.h"

const QStringList FrameSetup::kSettingsGroups = {
    "Defaults", "Frame", "Parameters", "Time", "Receivers", "Bounds"
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

void FrameSetup::saveToSettings(QSettings& settings)
{
    for (const auto& param : m_parameters)
    {
        settings.beginGroup(param.name);
        settings.setValue("Word", param.word + 1);
        settings.endGroup();
    }
}
