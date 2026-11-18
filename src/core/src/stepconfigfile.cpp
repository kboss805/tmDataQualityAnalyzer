/**
 * @file stepconfigfile.cpp
 * @brief Implementation of the step-config file reader.
 */

#include "stepconfigfile.h"

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "constants.h"
#include "textfile.h"
#include "textparse.h"
#include "tomldocument.h"

bool parseStepConfig(const std::filesystem::path& path, std::vector<StepDefinition>& out, std::string& error)
{
    out.clear();

    const TomlDocument doc = TomlDocument::load(path);
    if (!doc.ok())
    {
        error = "Could not open step-config file '" + textfile::utf8FileName(path) + "'.";
        return false;
    }

    // Each `[[Step]]` entry is one step, whatever keys it holds: one with no usable `db` keeps the
    // default of 0 and still counts. The key is matched without regard to case, a value that does
    // not read as a number is skipped (so an earlier good one stands), and the last good one wins.
    // Values arrive as written, quotes included, so `db = "6"` is not a number here.
    for (const TomlDocument::Table* table : doc.tables("step"))
    {
        StepDefinition step;
        for (const auto& [key, value] : table->entries)
        {
            double num = 0.0;
            if (textparse::asciiLower(key) == "db" && textparse::parseDouble(value, num))
            {
                step.db = num;
            }
        }
        out.push_back(step);
    }

    if (out.empty())
    {
        error = "No [[Step]] entries found in '" + textfile::utf8FileName(path) + "'.";
        return false;
    }
    return true;
}

std::string initialStepConfigPath(const std::string& appRoot, const std::string& preferred)
{
    // A string-in, string-out function rather than a std::filesystem::path one: the result
    // is shown to the operator ("<root>/settings/rcvr_cals/default.toml"), and
    // path / "x" would put a backslash in the middle of that on Windows.
    auto isFile = [](const std::string& utf8) {
        std::error_code ec; // the throwing overload would turn a bad path into a crash
        return std::filesystem::is_regular_file(textfile::pathFromUtf8(utf8), ec);
    };

    // A reference that no longer resolves - a template written on another machine,
    // a moved settings tree - falls through to the default rather than opening
    // the dialog on a file that is not there.
    if (!preferred.empty() && isFile(preferred))
    {
        return preferred;
    }
    if (appRoot.empty())
    {
        return {};
    }
    const std::string fallback = appRoot + "/" + SettingsConstants::kSettingsDirName + "/"
                                 + SettingsConstants::kRcvrCalsDirName + "/"
                                 + SettingsConstants::kDefaultTomlFilename;
    return isFile(fallback) ? fallback : std::string();
}
