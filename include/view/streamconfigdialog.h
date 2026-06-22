/**
 * @file streamconfigdialog.h
 * @brief Modal dialog listing every PCM stream in a loaded Chapter 10 file and
 *        capturing per-stream processing configuration.
 */

#ifndef STREAMCONFIGDIALOG_H
#define STREAMCONFIGDIALOG_H

#include <QDialog>
#include <QStringList>
#include <QVector>

#include "streamconfig.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

/**
 * @brief Lets the user choose which PCM streams to process and how.
 *
 * Main table has five columns: Process, Channel, Mode, Configure (gear button), Ready (status icon).
 * Clicking the gear opens a per-stream sub-dialog (Frame Lock Setup or Receiver SNR) whose
 * type is determined by the Mode combo.
 * Read configured streams back via configs() and timeChannelIndex() after the dialog is accepted.
 */
class StreamConfigDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param[in] configs            Initial per-stream configuration (one entry per PCM channel).
     * @param[in] toml_dir           Default directory for the TOML file browse dialogs.
     * @param[in] time_channels      List of available time channel display strings.
     * @param[in] time_channel_index Currently selected time channel index (1-based, 0 = none).
     * @param[in] time_channel_id    Resolved channel ID of the selected time channel (-1 = none),
     *                               inherited by the Receiver SNR calibration extraction (US3.2).
     * @param[in] app_root           Application root directory, used to locate the
     *                               settings/receiver_params, settings/rcvr_cals, and
     *                               settings/framesync_patterns directories so the relevant
     *                               "Load" file dialogs can open there by default.
     * @param[in] parent             Optional parent widget.
     */
    explicit StreamConfigDialog(const QVector<StreamConfig>& configs,
                                const QString& toml_dir,
                                const QStringList& time_channels,
                                int time_channel_index,
                                int time_channel_id,
                                const QString& app_root,
                                QWidget* parent = nullptr);

    /// @return The per-stream configuration as currently edited.
    QVector<StreamConfig> configs() const;

    /// @return Selected time channel index (1-based; matches MainViewModel convention).
    int timeChannelIndex() const;

private slots:
    void validateAndAccept();

private:
    /// Per-row widget handles and stored sub-dialog values.
    struct RowWidgets
    {
        QCheckBox*   process    = nullptr;
        QComboBox*   mode       = nullptr;
        QPushButton* gearBtn    = nullptr;
        QLabel*      readyLabel = nullptr;

        // Values updated when the sub-dialog is accepted
        QString frameSyncPattern = PCMConstants::kDefaultFrameSync;
        QString frameSyncMask    = PCMConstants::kDefaultFrameSyncMask;
        int     bitsInFrame      = PCMConstants::kDefaultBitsPerFrame;
        bool    randomized       = false;
        bool    inverted         = false;
        int     samplePeriodIndex = UIConstants::kDefaultSamplePeriodIndex;
        double  dataRateMbps     = 0.0;
        int     polarityIndex    = UIConstants::kDefaultPolarityIndex;
        int     slopeIndex       = UIConstants::kDefaultSlopeIndex;
        double  scaleDdBPerV     = PCMConstants::kDefaultScaleDdBPerV;
        int     numReceivers     = PCMConstants::kDefaultNumReceivers;
        int     receiverChannels = PCMConstants::kDefaultReceiverChannels;
        QString    receiverParamsToml;
        QHash<int, CalibrationProfile> calibrationByWord; ///< Non-linear step calibration profiles (US3.2).
        StreamMode lastConfiguredMode = StreamMode::FrameSyncLockStats; ///< Mode whose values are currently stored.
        bool       gearConfirmed      = false; ///< True only after the user has opened and accepted the gear dialog.
    };

    void buildTable();

    /// Refreshes the ready icon for @p row and re-evaluates the OK button state.
    void updateReadyIcon(int row);

    /// Enables the OK button iff at least one stream is ready (green check).
    void updateOkButton();

    /// Opens the appropriate sub-dialog for @p row based on the current mode selection.
    void openGearDialog(int row);

    QVector<StreamConfig> m_configs;
    QString               m_toml_dir;
    QString               m_app_root;
    QTableWidget*         m_table              = nullptr;
    QVector<RowWidgets>   m_rows;
    QComboBox*            m_time_channel_combo = nullptr;
    QPushButton*          m_ok_btn             = nullptr;
    int                   m_time_channel_id    = -1; ///< Resolved time channel ID for calibration extraction (US3.2).
};

#endif // STREAMCONFIGDIALOG_H

