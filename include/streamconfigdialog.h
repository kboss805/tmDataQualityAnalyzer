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
#include "timefields.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class TimeExtractionWidget;

/**
 * @brief Lets the user choose which PCM streams to process and how.
 *
 * Main table has five columns: Process, Channel, Mode, Setup (gear button), Ready (status icon).
 * Clicking the gear opens a per-stream sub-dialog (Frame Lock Setup or Receiver SNR) whose
 * type is determined by the Mode combo. Time range controls are embedded at the bottom.
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
     * @param[in] start_time         Initial start time (from the file's time range).
     * @param[in] stop_time          Initial stop time (from the file's time range).
     * @param[in] extract_all_time   Initial state of the "Extract All Time" checkbox.
     * @param[in] parent             Optional parent widget.
     */
    explicit StreamConfigDialog(const QVector<StreamConfig>& configs,
                                const QString& toml_dir,
                                const QStringList& time_channels,
                                int time_channel_index,
                                const TimeFields& start_time,
                                const TimeFields& stop_time,
                                bool extract_all_time,
                                QWidget* parent = nullptr);

    /// @return The per-stream configuration as currently edited.
    QVector<StreamConfig> configs() const;

    /// @return Selected time channel index (1-based; matches MainViewModel convention).
    int timeChannelIndex() const;

    /// @return Whether "Extract All Time" is checked.
    bool extractAllTime() const;

    /// @return Start time text in "DDD:HH:MM:SS" format.
    QString startTimeText() const;

    /// @return Stop time text in "DDD:HH:MM:SS" format.
    QString stopTimeText() const;

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
        int     samplePeriodIndex = UIConstants::kDefaultSamplePeriodIndex;
        double  dataRateMbps     = 0.0;
        int     polarityIndex    = UIConstants::kDefaultPolarityIndex;
        int     slopeIndex       = UIConstants::kDefaultSlopeIndex;
        double  scaleDdBPerV     = PCMConstants::kDefaultScaleDdBPerV;
        int     numReceivers     = PCMConstants::kDefaultNumReceivers;
        int     receiverChannels = PCMConstants::kDefaultReceiverChannels;
        QString    receiverParamsToml;
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
    QTableWidget*         m_table              = nullptr;
    QVector<RowWidgets>   m_rows;
    QComboBox*            m_time_channel_combo = nullptr;
    TimeExtractionWidget* m_time_widget        = nullptr;
    QPushButton*          m_ok_btn             = nullptr;
};

#endif // STREAMCONFIGDIALOG_H
