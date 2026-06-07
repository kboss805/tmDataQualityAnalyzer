/**
 * @file tst_timeextractionwidget.h
 * @brief Unit tests for TimeExtractionWidget signals, getters, and controls.
 */

#ifndef TST_TIMEEXTRACTIONWIDGET_H
#define TST_TIMEEXTRACTIONWIDGET_H

#include <QObject>

class TestTimeExtractionWidget : public QObject
{
    Q_OBJECT

private slots:
    void defaultExtractAllUnchecked();
    void extractAllTimeToggle();
    void fillTimesAndReadBack();
    void clearTimesEmptiesFields();
    void setAllEnabledDisablesFields();
};

#endif // TST_TIMEEXTRACTIONWIDGET_H
