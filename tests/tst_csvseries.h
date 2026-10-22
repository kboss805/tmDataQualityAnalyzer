/**
 * @file tst_csvseries.h
 * @brief Unit tests for the CSV series format: the importer (CsvSeriesParser) and the exporter
 *        (PlotViewModel::exportCsv today; the Qt-free writer after plan step 1.2), pinned through
 *        the view model so the same cases hold on both sides of the move.
 */

#ifndef TST_CSVSERIES_H
#define TST_CSVSERIES_H

#include <QObject>

class TestCsvSeries : public QObject
{
    Q_OBJECT

private slots:
    // Import, pinned against the QString / QTextStream parser BEFORE it moved (de-Qt, plan step 1.2):
    // what a hand-edited, re-saved or damaged file reads as.
    void importEdgeCasesKeepTheirRecordedBehaviour_data();
    void importEdgeCasesKeepTheirRecordedBehaviour();

    // The number reader the importer uses, against Qt's.
    void numbersReadLikeQt();

    // The Qt-free implementations against the ones they replaced, kept in the test as oracles and run
    // over generated input: files for the importer, series for the writer, times for the clock stamp.
    void theQtFreeImporterReadsEveryGeneratedFileLikeTheQtOne();
    void theQtFreeWriterWritesEveryGeneratedSeriesLikeTheQtOne();
    void theClockStampIsTheSameAsTheQStringOne();
    void aUtf16FileWithCrlfLineEndsLoads();

    // Export, pinned the same way: the exact bytes written.
    void exportEdgeCasesKeepTheirRecordedBehaviour_data();
    void exportEdgeCasesKeepTheirRecordedBehaviour();
    void aLargeExportCrossesTheFlushThresholdIntact();
    void anExportReplacesTheFileThatWasThere();
    void csvPathsMayBeNonAscii();
    void exportOfNothingWritesNothing();
    void exportToAnUnwritablePathFails();
    void exportThenImportThenExportIsByteIdentical();
};

#endif // TST_CSVSERIES_H
