/**
 * @file tst_csvseries.h
 * @brief Unit tests for what the plot does with the CSV series format: importing a file into the PlotViewModel
 *        (loadCsvFile) and exporting its series (exportCsv).
 *
 * The format itself - the parser, the writer, the clock stamp and the number reader - is the core's and is tested
 * without Qt in native/tmdq_core_tests/test_csvseries.cpp.
 */

#ifndef TST_CSVSERIES_H
#define TST_CSVSERIES_H

#include <QObject>

class TestCsvSeries : public QObject
{
    Q_OBJECT

private slots:
    // Import: the rows whose answer shows what the plot adds to the parser's result.
    void importEdgeCasesKeepTheirRecordedBehaviour_data();
    void importEdgeCasesKeepTheirRecordedBehaviour();

    // Export: how the plot's series become columns, and the file's handling through the plot.
    void exportEdgeCasesKeepTheirRecordedBehaviour_data();
    void exportEdgeCasesKeepTheirRecordedBehaviour();
    void csvPathsMayBeNonAscii();
    void exportOfNothingWritesNothing();
    void exportToAnUnwritablePathFails();
    void exportThenImportThenExportIsByteIdentical();
};

#endif // TST_CSVSERIES_H
