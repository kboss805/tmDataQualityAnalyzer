/**
 * @file receiverparamsfile.cpp
 * @brief Implementation of the receiver-parameters file reader and writer, and of the word-map resolution.
 */

#include "receiverparamsfile.h"

#include <filesystem>
#include <string>
#include <system_error>

#include "constants.h"
#include "textfile.h"
#include "tomldocument.h"

ReceiverParams readReceiverParams(const std::filesystem::path& path, const ReceiverParams& defaults)
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
    // as an explicit contains() rather than a nested default, which would read as
    // though [Receivers] were optional in a different sense than it is.
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

bool writeReceiverParams(const std::filesystem::path& path, const ReceiverParams& params, const FrameSetup& setup)
{
    // A file with no word map parses but yields no parameters, and every consumer
    // then rejects it. Refusing here is what keeps save and load inverses.
    if (setup.length() == 0)
    {
        return false;
    }

    // No delete first: TomlDocument::save() truncates, so an overwrite never leaves
    // the old file's word map behind alongside the new one.
    TomlDocument cfg;
    cfg.setValue("Parameters/Polarity", params.polarityIndex);
    cfg.setValue("Parameters/Slope",    params.slopeIndex);
    cfg.setValue("Parameters/Scale",    params.scaleDdBPerV);
    cfg.setValue("Receivers/Count",               params.numReceivers);
    cfg.setValue("Receivers/ChannelsPerReceiver", params.receiverChannels);
    setup.writeWordMap(cfg);
    return cfg.save(path);
}

bool saveReceiverParamsFile(const std::filesystem::path& path,
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
    if (!writeReceiverParams(path, params, setup))
    {
        const auto name = path.filename().u8string();
        error = "Could not write '" + std::string(name.begin(), name.end()) + "'.";
        return false;
    }
    return true;
}

std::filesystem::path defaultReceiverParamsFile(const std::string& appRoot)
{
    return textfile::pathFromUtf8(appRoot + "/" + SettingsConstants::kSettingsDirName + "/" +
                                  SettingsConstants::kReceiverParamsDirName + "/" + SettingsConstants::kDefaultTomlFilename);
}

bool resolveReceiverWordMap(FrameSetup& setup, const std::filesystem::path& explicitFile,
                            const std::filesystem::path& defaultFile, int numReceivers, int receiverChannels,
                            int wordsInMinorFrame, WordMapResolution& out)
{
    out = WordMapResolution{};
    setup.clearParameters();
    std::error_code ec; // exists() with an error code never throws on a bad path
    if (!explicitFile.empty())
    {
        if (!std::filesystem::exists(explicitFile, ec))
        {
            out.error = "Receiver Parameters file '" + textfile::utf8FileName(explicitFile) + "' was not found.";
            return false;
        }
        if (!setup.tryLoadingFile(explicitFile, wordsInMinorFrame))
        {
            out.error = "Failed to load Receiver Parameters from '" + textfile::utf8FileName(explicitFile) +
                        "'. Check the word map.";
            return false;
        }
    }
    else
    {
        bool loaded = false;
        if (!defaultFile.empty() && std::filesystem::exists(defaultFile, ec))
        {
            loaded = setup.tryLoadingFile(defaultFile, wordsInMinorFrame);
            if (!loaded)
            {
                out.warning = "the default Receiver Parameters file '" + textfile::utf8FileName(defaultFile) +
                              "' could not be used; the sequential word map for " + std::to_string(numReceivers) +
                              " x " + std::to_string(receiverChannels) + " receiver channels is used instead.";
            }
        }
        if (!loaded)
        {
            // A rejected load can leave a partial map behind and buildDefaultReceiverMap appends, so start empty.
            setup.clearParameters();
            if (!setup.buildDefaultReceiverMap(numReceivers, receiverChannels, wordsInMinorFrame, out.error))
            {
                return false;
            }
        }
    }
    if (setup.length() == 0)
    {
        out.error = "Receiver Parameters file contains no parameters.";
        return false;
    }
    return true;
}
