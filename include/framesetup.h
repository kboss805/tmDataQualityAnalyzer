/**
 * @file framesetup.h
 * @brief PCM frame parameter definitions and TOML-based frame setup loading.
 */

#ifndef FRAMESETUP_H
#define FRAMESETUP_H

#include <QObject>
#include <QSettings>

#include "calibrationprofile.h"

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
    QString name;      ///< Parameter name (e.g., "L_RCVR1").
    int word;          ///< Zero-based word index within the minor frame.
    double slope;      ///< Calibration slope (dB per raw count).
    double scale;      ///< Calibration offset applied before slope.
    bool is_enabled;   ///< Whether this parameter is included in output.
    double sample_sum; ///< Running sum of scaled values for averaging.
    CalibrationProfile profile; ///< Optional non-linear step calibration (US3.2); linear math used when invalid.
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
     * @brief Loads parameters from a TOML file.
     * @param[in] filename Path to the TOML file.
     * @param[in] num_words_in_minor_frame Number of words per minor frame.
     * @return true if at least one parameter was loaded.
     */
    bool tryLoadingFile(const QString& filename, int num_words_in_minor_frame);

    /// Saves the current parameter list to @p settings.
    void saveToSettings(QSettings& settings);

    int length() const; ///< @return Number of parameters.

    /// @return Mutable pointer to the parameter at index @p i.
    ParameterInfo* getParameter(int i);
    /// @return Const pointer to the parameter at index @p i.
    const ParameterInfo* getParameter(int i) const;

    /// Removes all parameters.
    void clearParameters();

    /// Appends a parameter with the given name and zero-based word index.
    void addParameter(const QString& name, int word);

private:
    /// Reads TOML section names in the order they appear in the file.
    static QStringList readGroupsInFileOrder(const QString& filename);

    static const QStringList kSettingsGroups; ///< Reserved TOML group names to skip.
    QList<ParameterInfo> m_parameters;        ///< Ordered list of frame parameters.
};

#endif // FRAMESETUP_H
