/**
 * @file tst_seriescolumnschema.h
 * @brief Unit tests for SeriesColumnSchema — the CSV column-header format and its
 *        inverse parser, pinned as a round-trip so export and import can't drift.
 */

#ifndef TST_SERIESCOLUMNSCHEMA_H
#define TST_SERIESCOLUMNSCHEMA_H

#include <QObject>

class TestSeriesColumnSchema : public QObject
{
    Q_OBJECT

private slots:
    void snrSeriesNameFormat();
    void columnHeaderAddsMetricSuffix();
    void columnHeaderSnrPassthrough();
    void parseLockHeader();
    void parseMissedHeader();
    void parseSnrHeader();
    void parseSnrLabelWithSpaces();
    void parseSnrShapeWinsOverSuffix();
    void parseUnknownFallsBackToSnr();
    void roundTripLockMissedSnr();
};

#endif // TST_SERIESCOLUMNSCHEMA_H
