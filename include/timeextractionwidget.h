/**
 * @file timeextractionwidget.h
 * @brief Widget with time range selection controls.
 */

#ifndef TIMEEXTRACTIONWIDGET_H
#define TIMEEXTRACTIONWIDGET_H

#include <QCheckBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>

#include "timefields.h"

/**
 * @brief Widget containing the extract-all toggle and start/stop time inputs.
 */
class TimeExtractionWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TimeExtractionWidget(QWidget* parent = nullptr);

    bool extractAllTime() const;          ///< @return True if "Extract All Time" is checked.
    void setExtractAllTime(bool value);   ///< Sets the "Extract All Time" checkbox.

    /// Enables or disables all controls in the widget.
    void setAllEnabled(bool enabled);

    /**
     * @brief Populates start/stop time fields from file times.
     * @param[in] start Start time components.
     * @param[in] stop  Stop time components.
     */
    void fillTimes(const TimeFields& start, const TimeFields& stop);

    /// Clears all start/stop time fields.
    void clearTimes();

    QString startTimeText() const;        ///< @return Start time text in "DDD:HH:MM:SS" format.
    QString stopTimeText() const;         ///< @return Stop time text in "DDD:HH:MM:SS" format.

signals:
    void extractAllTimeChanged(bool checked);   ///< Emitted when the "Extract All Time" checkbox is toggled.
    void startTimeEditingFinished();            ///< Emitted when the user finishes editing the start time field.
    void stopTimeEditingFinished();             ///< Emitted when the user finishes editing the stop time field.

private:
    QCheckBox* m_time_all;    ///< "Extract all time" toggle checkbox.
    QLineEdit* m_start_time;  ///< Start time input (DDD:HH:MM:SS).
    QLineEdit* m_stop_time;   ///< Stop time input (DDD:HH:MM:SS).
};

#endif // TIMEEXTRACTIONWIDGET_H
