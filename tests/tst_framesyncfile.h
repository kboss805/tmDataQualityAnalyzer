/**
 * @file tst_framesyncfile.h
 * @brief Unit tests for the frame-sync pattern file (US1.0 / US2.0): what loading and saving one does,
 *        pinned through the dialog's own load/save functions so the same cases hold on both sides of
 *        the move of the file format into the core (plan step 1.4).
 */

#ifndef TST_FRAMESYNCFILE_H
#define TST_FRAMESYNCFILE_H

#include <QObject>

class TestFrameSyncFile : public QObject
{
    Q_OBJECT

private slots:
    // Loading, pinned against the widget-bound implementation BEFORE the format moved to the core.
    void loadEdgeCasesKeepTheirRecordedBehaviour_data();
    void loadEdgeCasesKeepTheirRecordedBehaviour();

    // Saving: the exact bytes written.
    void saveEdgeCasesKeepTheirRecordedBehaviour_data();
    void saveEdgeCasesKeepTheirRecordedBehaviour();

    // The Receiver SNR defaults read from framesync_rcvr_default.toml.
    void receiverDefaultsEdgeCasesKeepTheirRecordedBehaviour_data();
    void receiverDefaultsEdgeCasesKeepTheirRecordedBehaviour();

    // The core's reader and writer, and the implementations they replaced (kept in the test) compared
    // over generated files.
    void theReaderReportsWhatAFileSaysAndNothingElse();
    void writingToAnUnwritablePathFails();
    void theCoreReadsEveryGeneratedFileLikeTheOldDialogCode();
    void theCoreWritesEveryGeneratedFileLikeTheOldDialogCode();
    void aFileWrittenByTheDialogLoadsBackIntoIt();
};

#endif // TST_FRAMESYNCFILE_H
