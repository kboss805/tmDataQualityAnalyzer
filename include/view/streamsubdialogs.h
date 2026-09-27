/**
 * @file streamsubdialogs.h
 * @brief Per-mode sub-dialogs for StreamConfigDialog (Frame Lock Setup,
 *        Calibration Setup, Receiver SNR), and the layout vocabulary the three
 *        of them share.
 *
 * Declarations only - the implementations are in streamsubdialogs.cpp. These
 * classes used to be an anonymous namespace inside this header, private to the
 * single translation unit that included it, which is why no test could build
 * one: the receiver-parameters Save/Load round trip had to be checked by hand.
 *
 * The widget includes below are load-bearing rather than incidental - the
 * inline accessors read their widgets, so forward declarations do not suffice.
 */

#ifndef STREAMSUBDIALOGS_H
#define STREAMSUBDIALOGS_H

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QHash>
#include <QLineEdit>
#include <QSpinBox>
#include <QString>

#include "calibrationextractor.h"
#include "calibrationprofile.h"
#include "constants.h"
#include "framesyncparams.h"
#include "stepdetector.h"
#include "streamconfig.h"

class QGridLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QWidget;

/// Shared layout metrics so every stream dialog uses the same vertical rhythm and
/// grid geometry. Previously each dialog hand-picked these (outer spacing varied
/// 4/6/8 across the three; the input→Load/Save gap column and button offsets were
/// re-declared per call site), which is what let the look-and-feel drift.
namespace DialogLayout {
    constexpr int kOuterSpacing     = 6;   ///< Spacing between a dialog's top-level rows.
    constexpr int kSectionGap       = 16;  ///< Larger gap that introduces a logical group.
    constexpr int kControlGap       = 8;   ///< Small gap between closely related controls.
    constexpr int kGridHSpacing     = 10;  ///< Horizontal spacing inside a form grid.
    constexpr int kGridVSpacing     = 4;   ///< Vertical spacing inside a form grid.
    constexpr int kColGapWidth      = 16;  ///< Width of the gap column before Load/Save buttons.
    constexpr int kIconButtonSize   = 28;  ///< Standard icon-button edge (gear, load, save).
    constexpr int kCheckboxLabelGap = 6;   ///< Gap between a checkbox and its text label.
}

/// Loads frame sync fields from a TOML file into the given widgets. Handles both
/// the current BitsPerFrame key and the older WordsInMinorFrame one.
///
/// Randomized is applied only when the file carries the key: files written before
/// it was persisted (including the shipped PRN patterns) must not silently clear a
/// choice the operator has already made.
void loadFrameSyncFromToml(const QString& filename,
                           QLineEdit* syncEdit,
                           QLineEdit* maskEdit,
                           QSpinBox*  bitsSpinBox,
                           QString&   toml_dir,
                           QCheckBox* invertedBox = nullptr,
                           QCheckBox* randomizedBox = nullptr);

/// Saves frame sync fields to a TOML file: the pattern, the mask, the frame length,
/// and the two stream-format flags (Invert Data, Derandomize). Data Rate and Sample
/// Rate stay out - they are per-session operator inputs (US1.0).
void saveFrameSyncToToml(const QString& filename,
                         const QString& syncPattern,
                         const QString& syncMask,
                         int            bitsPerFrame,
                         bool           inverted,
                         bool           randomized,
                         QString&       toml_dir);

void styleIconButton(QPushButton* button, int size);

/// Default Receiver SNR frame parameters (pattern, mask, bits-per-frame),
/// loaded from settings/framesync_patterns/framesync_rcvr_default.toml so they are
/// user-editable rather than hard-coded. Falls back to the compiled constants
/// if the file is missing or a field is absent/invalid.
struct ReceiverFrameDefaults
{
    QString pattern = PCMConstants::kDefaultReceiverSNRPattern;
    QString mask    = PCMConstants::kDefaultFrameSyncMask;
    int     bits    = PCMConstants::kDefaultReceiverSNRBits;
};

ReceiverFrameDefaults loadReceiverFrameDefaults(const QString& app_root);

/// Bundles the frame-sync input widgets shared by the Frame Sync Lock and
/// Receiver SNR dialogs so a single builder can create them.
struct FrameSyncWidgets
{
    QLineEdit*      syncPattern  = nullptr;
    QLineEdit*      syncMask     = nullptr;
    QSpinBox*       bitsPerFrame = nullptr;
    QDoubleSpinBox* dataRate     = nullptr;
    QComboBox*      sampleRate   = nullptr;
};

/// Cancel + primary action button pair shared by every stream dialog.
struct DialogButtons
{
    QPushButton* cancel  = nullptr;
    QPushButton* primary = nullptr;
};

/// Creates a Cancel button (pre-wired to reject @p dialog) and a blue, default
/// primary button labelled @p primaryText. The caller wires the primary button's
/// click handler and arranges both in a layout.
DialogButtons makeDialogButtons(QDialog* dialog, const QString& primaryText);

/// Appends the standard dialog footer: an optional left-aligned toggle (with an
/// optional adjacent label), a stretch, then the primary button and Cancel. Used
/// by every stream dialog so the footer is built one way.
void addBottomBar(QVBoxLayout* layout, const DialogButtons& buttons, QWidget* parent,
                  QCheckBox* leftToggle = nullptr, const QString& toggleLabel = QString());

////////////////////////////////////////////////////////////////////////////////
//                        FRAME LOCK SETUP DIALOG                             //
////////////////////////////////////////////////////////////////////////////////

class FrameLockSetupDialog : public QDialog
{
public:
    explicit FrameLockSetupDialog(const StreamConfig& cfg,
                                  const QString& toml_dir,
                                  const QString& app_root,
                                  QWidget* parent = nullptr);

    QString frameSyncPattern() const { return m_fs.syncPattern->text().trimmed().toUpper(); }
    QString frameSyncMask()    const { return m_fs.syncMask->text().trimmed().toUpper(); }
    int     bitsPerFrame()     const { return m_fs.bitsPerFrame->value(); }
    bool    randomized()        const { return m_randomized->isChecked(); }
    bool    inverted()          const { return m_inverted->isChecked(); }
    int     samplePeriodIndex() const { return m_fs.sampleRate->currentIndex(); }
    double  dataRateMbps()      const { return m_fs.dataRate->value(); }
    QString lastTomlDir()       const { return m_toml_dir; }
    bool    applyToAll()        const { return m_apply_to_all->isChecked(); }

private:
    FrameSyncWidgets m_fs; ///< Frame Sync / Mask / Bits Per Frame / Data Rate / Average Period.
    QCheckBox*      m_randomized   = nullptr;
    QCheckBox*      m_inverted     = nullptr;
    QCheckBox*      m_apply_to_all   = nullptr;
    QString         m_toml_dir;
    QString         m_app_root;
};

////////////////////////////////////////////////////////////////////////////////
//                      CALIBRATION SETUP DIALOG (US5.3)                       //
////////////////////////////////////////////////////////////////////////////////

/// Collects the two files needed for non-linear calibration extraction — the
/// step-config TOML and the calibration Chapter 10 file — and produces the
/// per-channel calibration profiles directly:
///   * Step config: the [[Step]] table format must parse.
///   * Cal Ch10: as soon as both files are present, the whole file is processed
///     (CalibrationExtractor) to detect the step plateaus and build the
///     profiles. This is the single, authoritative pass — there is no separate
///     pre-flight lock check and nothing further to process on OK.
/// OK is enabled once extraction has produced at least one valid profile. The
/// owner reads calibrationByWord() on accept.
class CalibrationSetupDialog : public QDialog
{
public:
    /// @param baseRequest Acquisition + receiver fields already filled by the
    ///        caller (frame sync, channel IDs, word-map source). The dialog adds
    ///        the cal file, parsed steps, and clip seconds before extracting.
    /// @param initialStepPath The step file this stream last used, if any. The
    ///        dialog opens with it, or with the shipped default if it is empty or
    ///        gone (StepDetector::initialStepConfigPath).
    CalibrationSetupDialog(const CalibrationExtractor::Request& baseRequest,
                           const QString& tomlDir,
                           const QString& appRoot,
                           const QString& initialStepPath = QString(),
                           QWidget* parent = nullptr);

    /// Per-channel calibration profiles produced by the whole-file extraction,
    /// keyed by word index. Valid after the dialog is accepted.
    QHash<int, CalibrationProfile> calibrationByWord() const { return m_calibration_by_word; }
    QString lastTomlDir() const { return m_toml_dir; }

    /// Operator-facing report naming which receivers calibrated and why the rest
    /// fell back. Valid after the dialog is accepted.
    QString calibrationReport() const { return m_calibration_report; }

    // Input references: what produced the profiles above, so a processing template
    // can store them for a later re-extraction pass.
    QString calFilePath()  const { return m_cal_path; }
    QString stepFilePath() const { return m_step_path; }
    double  clipStartSec() const { return m_clip_start ? m_clip_start->value() : 0.0; }
    double  clipEndSec()   const { return m_clip_end   ? m_clip_end->value()   : 0.0; }

private:
    enum StatusKind { Pending, Ok, Warn, Fail };

    void setStatus(QLabel* label, StatusKind kind, const QString& text);

    void updateOk();

    void onBrowseStep();

    /// Selects @p path as the step file and parses it. @p automatic marks a file
    /// the dialog preloaded rather than one the operator chose, so the status can
    /// say so.
    void loadStepFile(const QString& path, bool automatic);

    void onBrowseCal();

    /// Runs the whole-file calibration extraction once both inputs are present.
    /// This is the single processing pass: it detects the step plateaus and
    /// builds the per-channel profiles. OK simply hands these back to the owner.
    void maybeRunExtraction();

    // Caller-supplied acquisition + receiver settings forwarded to the extractor;
    // the dialog only adds the cal file, steps, and clip seconds before running.
    CalibrationExtractor::Request m_base_request;
    QString m_toml_dir;
    QString m_app_root;

    // Selected files + parsed steps.
    QString m_step_path;
    QString m_cal_path;
    QVector<StepDefinition> m_steps;
    bool    m_step_ok = false;
    bool    m_cal_ok  = false;

    // Extracted profiles, keyed by word index (populated by maybeRunExtraction).
    QHash<int, CalibrationProfile> m_calibration_by_word;
    QString m_calibration_report; ///< Receiver-grouped outcome report for the summary box.

    // Widgets.
    QLabel*      m_step_path_label = nullptr;
    QLabel*      m_step_status    = nullptr;
    QLabel*      m_cal_path_label  = nullptr;
    QLabel*      m_cal_status     = nullptr;
    QPushButton* m_cal_browse     = nullptr;
    QPushButton* m_ok_button      = nullptr;
    QDoubleSpinBox* m_clip_start  = nullptr; ///< Seconds to clip from the start before step detection.
    QDoubleSpinBox* m_clip_end    = nullptr; ///< Seconds to clip from the end before step detection.
};

////////////////////////////////////////////////////////////////////////////////
//                         RECEIVER SNR DIALOG                                //
////////////////////////////////////////////////////////////////////////////////

class ReceiverSNRDialog : public QDialog
{
public:
    explicit ReceiverSNRDialog(const StreamConfig& cfg,
                               const QString& toml_dir,
                               int time_channel_id,
                               const QString& app_root,
                               bool swap_bytes,
                               QWidget* parent = nullptr);

    QString frameSyncPattern()   const { return m_fs.syncPattern->text().trimmed().toUpper(); }
    QString frameSyncMask()      const { return m_fs.syncMask->text().trimmed().toUpper(); }
    int     bitsPerFrame()       const { return m_fs.bitsPerFrame->value(); }
    bool    randomized()         const { return m_randomized->isChecked(); }
    bool    inverted()           const { return m_inverted->isChecked(); }
    int     samplePeriodIndex()    const { return m_fs.sampleRate->currentIndex(); }
    double  dataRateMbps()       const { return m_fs.dataRate->value(); }
    /// The acquisition fields as a bundle (for building a CalibrationExtractor::Request).
    FrameSyncParams frameSyncParams() const {
        return { frameSyncPattern(), frameSyncMask(), bitsPerFrame(),
                 randomized(), inverted(), dataRateMbps() };
    }
    int     polarityIndex()      const { return m_polarity->currentIndex(); }
    int     slopeIndex()         const { return m_slope->currentIndex(); }
    double  scaleDdBPerV()       const { return m_scale->value(); }
    int     numReceivers()       const { return m_num_receivers->value(); }
    int     receiverChannels()   const { return m_receiver_channels->value(); }
    QString receiverParamsToml() const { return m_receiver_params_toml; }
    QString lastTomlDir()        const { return m_toml_dir; }
    bool    applyToAll()         const { return m_apply_to_all->isChecked(); }
    QHash<int, CalibrationProfile> calibrationByWord() const { return m_calibration_by_word; }

    // Input references: captured from the setup sub-dialog on extraction so a
    // processing template can store them alongside the config.
    QString calCh10Path()  const { return m_cal_ch10_path; }
    QString stepTomlPath() const { return m_step_toml_path; }
    double  clipStartSec() const { return m_clip_start_sec; }
    double  clipEndSec()   const { return m_clip_end_sec; }

private:
    void updateCalibrationLabel();

    /// Opens the calibration setup dialog. The dialog processes the calibration
    /// file in full as soon as both inputs are loaded and exposes the resulting
    /// per-channel profiles; here we simply adopt them on accept (US5.3).
    void onExtractCalibration();

    FrameSyncWidgets m_fs; ///< Frame Sync / Mask / Bits Per Frame / Data Rate / Average Period.
    QCheckBox*      m_randomized       = nullptr;
    QCheckBox*      m_inverted         = nullptr;
    QComboBox*      m_polarity         = nullptr;
    QComboBox*      m_slope            = nullptr;
    QDoubleSpinBox* m_scale            = nullptr;
    QSpinBox*       m_num_receivers     = nullptr;
    QSpinBox*       m_receiver_channels = nullptr;
    QCheckBox*      m_apply_to_all       = nullptr;
    QString         m_receiver_params_toml;
    QString         m_toml_dir;
    QString         m_app_root;

    // Non-linear step calibration (US5.3)
    int             m_time_channel_id = -1;     ///< Time channel ID inherited from the parent dialog.
    bool            m_swap_bytes = false;      ///< File-level byte order, inherited from the parent dialog.
    int             m_pcm_channel_id  = -1;     ///< PCM channel ID of the stream being calibrated.
    QHash<int, CalibrationProfile> m_calibration_by_word; ///< Extracted profiles, keyed by word index.
    QString         m_cal_ch10_path;   ///< Calibration input reference (serialized in a template).
    QString         m_step_toml_path;  ///< Calibration input reference (serialized in a template).
    double          m_clip_start_sec = 0.0; ///< Calibration input reference (serialized in a template).
    double          m_clip_end_sec   = 0.0; ///< Calibration input reference (serialized in a template).
    QLabel*         m_calibration_label = nullptr; ///< Status text for the calibration section.
};

#endif // STREAMSUBDIALOGS_H
