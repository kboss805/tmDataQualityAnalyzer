/**
 * @file framesetup.h
 * @brief PCM frame parameter definitions and the word map: the list of parameters, loaded from and written to TOML.
 *
 * The receiver-parameters FILE as a whole (the scalar settings beside the word map) is receiverparamsfile.h's; the
 * naming rules for receiver parameters are receivernames.h's (after this module's second step).
 */

#ifndef FRAMESETUP_H
#define FRAMESETUP_H

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "calibrationprofile.h"
#include "constants.h"
#include "tomldocument.h"

/**
 * @brief Describes one named parameter within a PCM minor frame.
 *
 * Each parameter maps to a word position in the frame and carries calibration
 * values (slope/scale) used to convert raw 16-bit samples to engineering units.
 * When @c profile.valid is true the non-linear step calibration (piecewise
 * interpolation) is used instead of the linear slope/scale model.
 */
struct ParameterInfo
{
    std::string name;           ///< Parameter name (e.g., "L_RCVR1"), UTF-8.
    int word = -1;              ///< Zero-based word index within the minor frame.
    double slope = 0.0;         ///< Calibration slope (dB per raw count).
    double scale = 0.0;         ///< Calibration offset applied before slope.
    CalibrationProfile profile; ///< Optional non-linear step calibration (US5.3); linear math used when invalid.
};

/**
 * @brief Loads and manages the list of PCM frame parameters.
 *
 * Parameters are read from a TOML file that maps receiver/channel names
 * to word positions within the PCM minor frame. The class also supports
 * saving the current parameter configuration back to a TOML file.
 *
 * The text API is UTF-8 `std::string` and paths are `std::filesystem::path`.
 * A plain class: every owner holds it by `shared_ptr` or deletes it itself, and it takes
 * its calibration profiles in a `std::unordered_map`.
 */
class FrameSetup
{
public:
    FrameSetup() = default;

    /**
     * @brief Loads the word map from a receiver-parameters TOML file.
     * @param[in] filename Path to the TOML file.
     * @param[in] num_words_in_minor_frame Number of words per minor frame.
     * @return true if the file parsed — NOT that it yielded any parameters.
     *
     * A file holding only metadata sections (a [Parameters] block and no word-map
     * groups) parses successfully and leaves length() == 0, so every caller must
     * check length() before using the setup. Both do, and each reports a clearer
     * cause than a bare false would allow ("contains no parameters" vs. "check the
     * word map"). writeReceiverParams() refuses to write such a file in the
     * first place.
     */
    bool tryLoadingFile(const std::filesystem::path& filename, int num_words_in_minor_frame);

    /// Writes the current parameter list into @p doc as one [Name] section per
    /// parameter, each carrying its 1-based Word. The caller owns saving the doc.
    void writeWordMap(TomlDocument& doc) const;

    /// @return Words per minor frame for @p bits_in_minor_frame, rounded up: a
    /// frame whose last word is partially filled still occupies that word's slot.
    /// Single-sourced because the word map's bounds check depends on it.
    static int wordsInMinorFrame(int bits_in_minor_frame);

    int length() const; ///< @return Number of parameters.

    /// @return Mutable pointer to the parameter at index @p i.
    ParameterInfo* getParameter(int i);
    /// @return Const pointer to the parameter at index @p i.
    const ParameterInfo* getParameter(int i) const;

    /// Removes all parameters.
    void clearParameters();

    /// Appends a parameter with the given name and zero-based word index.
    void addParameter(const std::string& name, int word);

    /// Channel-position prefix for a 0-based channel index: "L"/"R"/"C" for the
    /// known positions, "CH<n+1>" beyond them.
    static std::string channelPrefix(int channel_index);

    /// Canonical default parameter name "<prefix>_RCVR<receiver+1>" for a 0-based
    /// channel and receiver index. The single source of truth shared by the main
    /// processing run and the calibration extractor so their default word maps
    /// agree on names (a divergence silently misattaches calibration profiles).
    static std::string receiverParameterName(int channel_index, int receiver_index);

    /// Inverse of receiverParameterName(): the 1-based receiver number carried by
    /// a parameter name's "_RCVR<N>" suffix, or 0 when the name carries none.
    ///
    /// Returning 0 rather than failing is deliberate: a receiver-params TOML may
    /// name its parameters anything at all, so callers that group by receiver
    /// (the calibration summary) must degrade to listing the names themselves
    /// rather than inventing a receiver number.
    static int receiverIndexFromName(const std::string& name);

    /// The number after the LAST "_RCVR" in a name, as written: 0 when there is no token or the text after it is not
    /// a whole decimal number, and a negative number kept as it is. The series builder and the CSV column schema
    /// read names with this (a hand-edited CSV header "_RCVR-7" was a receiver -7 in v2.14.2, and the recorded
    /// digests of SeriesColumnSchema keep it so); receiverIndexFromName() is this, with a number that is not
    /// positive made 0, because a calibration summary has no use for a receiver 0 or -7.
    static int receiverNumberInName(const std::string& name);

    /// The channel-position part of a "<prefix>_RCVR<N>" name ("L", "R", "C", …),
    /// or the whole name when it carries no "_RCVR<N>" suffix.
    static std::string channelPartOfName(const std::string& name);

    /// Builds the default receiver word map — NumReceivers x ReceiverChannels
    /// parameters named via receiverParameterName(), assigned sequential words —
    /// into this FrameSetup (appending to any existing parameters). Returns false
    /// with a base message in @p error if the requested parameters don't fit the
    /// frame (the final word slot is the sync word, hence the strict "<" bound).
    bool buildDefaultReceiverMap(int num_receivers, int receiver_channels,
                                 int num_words_in_minor_frame, std::string& error);

    /// Applies the linear voltage→dB calibration (slope/scale) to every parameter
    /// and enables it for output. @p polarity_index 0=positive, 1=negative;
    /// @p slope_index selects the voltage range (ReceiverConstants::kSlopeVoltageLower/Upper).
    /// Inputs must be pre-validated by the caller. Lives in the core so the
    /// conversion is single-sourced and unit-testable, not reimplemented by a host.
    void applyLinearCalibration(int polarity_index, int slope_index, double scale_dB_per_V);

    /// Attaches non-linear step-calibration profiles (US5.3) by word index; only
    /// profiles flagged valid are attached, others keep their linear slope/scale.
    /// @return how many profiles were attached.
    int attachCalibrationProfiles(const std::unordered_map<int, CalibrationProfile>& by_word);

private:
    static const std::vector<std::string> kSettingsGroups; ///< Reserved TOML group names to skip.
    std::vector<ParameterInfo> m_parameters;               ///< Ordered list of frame parameters.
};

#endif // FRAMESETUP_H
