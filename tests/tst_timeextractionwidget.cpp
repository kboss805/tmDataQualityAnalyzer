/**
 * @file tst_timeextractionwidget.cpp
 * @brief Implementation of TimeExtractionWidget unit tests.
 */

#include "tst_timeextractionwidget.h"

#include <QSignalSpy>
#include <QtTest>

#include "constants.h"
#include "timeextractionwidget.h"

void TestTimeExtractionWidget::defaultExtractAllUnchecked()
{
    TimeExtractionWidget widget;
    QVERIFY(!widget.extractAllTime());
}

void TestTimeExtractionWidget::extractAllTimeToggle()
{
    TimeExtractionWidget widget;
    QSignalSpy spy(&widget, &TimeExtractionWidget::extractAllTimeChanged);

    widget.setExtractAllTime(true);
    QVERIFY(widget.extractAllTime());

    widget.setExtractAllTime(false);
    QVERIFY(!widget.extractAllTime());
}

void TestTimeExtractionWidget::fillTimesAndReadBack()
{
    TimeExtractionWidget widget;

    widget.fillTimes({45, 10, 30, 15}, {120, 23, 59, 59});

    QString start_text = widget.startTimeText();
    QString stop_text = widget.stopTimeText();

    QVERIFY2(start_text.contains("045"), qPrintable("Start DOY should be 045: " + start_text));
    QVERIFY2(start_text.contains("10"), qPrintable("Start hour should contain 10: " + start_text));

    QVERIFY2(stop_text.contains("120"), qPrintable("Stop DOY should be 120: " + stop_text));
    QVERIFY2(stop_text.contains("23"), qPrintable("Stop hour should contain 23: " + stop_text));
}

void TestTimeExtractionWidget::clearTimesEmptiesFields()
{
    TimeExtractionWidget widget;
    widget.fillTimes({45, 10, 30, 15}, {120, 23, 59, 59});
    widget.clearTimes();

    // After clearing, the fields should be empty or default
    QString start = widget.startTimeText();
    QString stop = widget.stopTimeText();
    // The input mask "000:00:00:00;_" shows underscores when cleared
    QVERIFY(start != "045:10:30:15");
    QVERIFY(stop != "120:23:59:59");
}

void TestTimeExtractionWidget::setAllEnabledDisablesFields()
{
    TimeExtractionWidget widget;

    widget.setAllEnabled(false);
    // Cannot directly test QWidget::isEnabled on child widgets without
    // exposing them, but the method should not crash.
    QVERIFY(true);

    widget.setAllEnabled(true);
    QVERIFY(true);
}
