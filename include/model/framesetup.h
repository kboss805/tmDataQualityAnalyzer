/**
 * @file framesetup.h
 * @brief PCM frame parameter definitions and TOML-based frame setup loading.
 */

#ifndef FRAMESETUP_H
#define FRAMESETUP_H

#include <QHash>
#include <QObject>
#include <QSettings>

#include "calibrationprofile.h"
#include "constants.h"

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
    QString name;               ///< Parameter name (e.g., "L_RCVR1").
    int word = -1;              ///< Zero-based word index within the minor frame.
    double slope = 0.0;         ///< Calibration slope (dB per raw count).
    double scale = 0.0;         ///< Calibration offset applied before slope.
    bool is_enabled = false;    ///< Whether this parameter is included in output.
    double sample_sum = 0.0;    ///< Running sum of RAW counts in the window; calibration is applied once to the windowed average.
    CalibrationProfile profile; ///< Optional non-linear step calibration (US5.3); linear math used when invalid.
};

/**
 * @brief The scalar settings a receiver-parameters TOML carries alongside its
 *        word map.
 *
 * These are the five values the Receiver SNR dialog shows above the word map.
 * They live here, next to the word-map reader, so the file's two halves are
 * read and written in one place: a save that wrote only these produced a file
 * every consumer rejects for having no parameters.
 */
struct ReceiverParams
{
    int    polarityIndex    = UIConstants::kDefaultPolarityIndex;    ///< 0 = positive, 1 = negative.
    int    slopeIndex       = UIConstants::kDefaultSlopeIndex;       ///< Index into the voltage ranges.
    double scaleDdBPerV     = PCMConstants::kDefaultScaleDdBPerV;    ///< Calibration scale, dB per volt.
    int    numReceivers     = PCMConstants::kDefaultNumReceivers;    ///< Receivers contributing channels.
    int    receiverChannels = PCMConstants::kDefaultReceiverChannels;///< Channels per receiver.
};

/**
 * @brief Loads and manages the list of PCM frame parameters.
 *
 * Parameters are read from a TOML file that maps receiver/channel names
 * to word positions within the PCM minor frame. The class also supports
 * saving the current parameter configuration back to a QSettings file.
 */
class FrameSetup : public QObject
{
    Q_OBJECT

public:
    explicit FrameSetup(QObject* parent = nullptr);

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
     * word map"). saveReceiverParamsFile() refuses to write such a file in the
     * first place.
     */
    bool tryLoadingFile(const QString& filename, int num_words_in_minor_frame);

    /// Saves the current parameter list to @p settings.
    void saveToSettings(QSettings& settings) const;

    /// @return Words per minor frame for @p bits_in_minor_frame, rounded up: a
    /// frame whose last word is partially filled still occupies that word's slot.
    /// Single-sourced because the word map's bounds check depends on it.
    static int wordsInMinorFrame(int bits_in_minor_frame);

    /// Reads the scalar settings from a receiver-parameters TOML (the word map is
    /// loaded separately by tryLoadingFile()). A key the file does not carry keeps
    /// its value from @p defaults, so a caller can pass what it is already showing
    /// and have a partial file change only what it actually specifies.
    static ReceiverParams readReceiverParams(const QString& path,
                                             const ReceiverParams& defaults = ReceiverParams());

    /// Writes a complete receiver-parameters TOML — the scalars plus this setup's
    /// word map. Refuses (returns false) when there is no word map to write.
    bool writeReceiverParams(const QString& path, const ReceiverParams& params) const;

    /// Writes a complete receiver-parameters file the way the Receiver SNR dialog
    /// needs it: the word map comes from @p word_map_source when that names a
    /// readable file, and otherwise is the default map for the params' receiver
    /// counts — i.e. whichever map the stream would actually process with.
    /// @return false with a reason in @p error if no usable map could be built or
    ///         the file could not be written.
    static bool saveReceiverParamsFile(const QString& path,
                                       const ReceiverParams& params,
                                       const QString& word_map_source,
                                       int num_words_in_minor_frame,
                                       QString& error);

    int length() const; ///< @return Number of parameters.

    /// @return Mutable pointer to the parameter at index @p i.
    ParameterInfo* getParameter(int i);
    /// @return Const pointer to the parameter at index @p i.
    const ParameterInfo* getParameter(int i) const;

    /// Removes all parameters.
    void clearParameters();

    /// Appends a parameter with the given name and zero-based word index.
    void addParameter(const QString& name, int word);

    /// Channel-position prefix for a 0-based channel index: "L"/"R"/"C" for the
    /// known positions, "CH<n+1>" beyond them.
    static QString channelPrefix(int channel_index);

    /// Canonical default parameter name "<prefix>_RCVR<receiver+1>" for a 0-based
    /// channel and receiver index. The single source of truth shared by the main
    /// processing run and the calibration extractor so their default word maps
    /// agree on names (a divergence silently misattaches calibration profiles).
    static QString receiverParameterName(int channel_index, int receiver_index);

    /// Inverse of receiverParameterName(): the 1-based receiver number carried by
    /// a parameter name's "_RCVR<N>" suffix, or 0 when the name carries none.
    ///
    /// Returning 0 rather than failing is deliberate: a receiver-params TOML may
    /// name its parameters anything at all, so callers that group by receiver
    /// (the calibration summary) must degrade to listing the names themselves
    /// rather than inventing a receiver number.
    static int receiverIndexFromName(const QString& name);

    /// The channel-position part of a "<prefix>_RCVR<N>" name ("L", "R", "C", …),
    /// or the whole name when it carries no "_RCVR<N>" suffix.
    static QString channelPartOfName(const QString& name);

    /// Builds the default receiver word map — NumReceivers x ReceiverChannels
    /// parameters named via receiverParameterName(), assigned sequential words —
    /// into this FrameSetup (appending to any existing parameters). Returns false
    /// with a base message in @p error if the requested parameters don't fit the
    /// frame (the final word slot is the sync word, hence the strict "<" bound).
    bool buildDefaultReceiverMap(int num_receivers, int receiver_channels,
                                 int num_words_in_minor_frame, QString& error);

    /// Applies the linear voltage→dB calibration (slope/scale) to every parameter
    /// and enables it for output. @p polarity_index 0=positive, 1=negative;
    /// @p slope_index selects the voltage range (UIConstants::kSlopeVoltageLower/Upper).
    /// Inputs must be pre-validated by the caller. Lives in the Model so the
    /// conversion is single-sourced and unit-testable, not embedded in the ViewModel.
    void applyLinearCalibration(int polarity_index, int slope_index, double scale_dB_per_V);

    /// Attaches non-linear step-calibration profiles (US5.3) by word index; only
    /// profiles flagged valid are attached, others keep their linear slope/scale.
    /// @return how many profiles were attached.
    int attachCalibrationProfiles(const QHash<int, CalibrationProfile>& by_word);

private:
    /// Reads TOML section names in the order they appear in the file.
    static QStringList readGroupsInFileOrder(const QString& filename);

    static const QStringList kSettingsGroups; ///< Reserved TOML group names to skip.
    QList<ParameterInfo> m_parameters;        ///< Ordered list of frame parameters.
};

#endif // FRAMESETUP_H
