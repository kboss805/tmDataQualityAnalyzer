/**
 * @file tst_framesyncfile.h
 * @brief Unit tests for what the dialogs do with a frame-sync pattern file (US1.0 / US2.0): loading one
 *        into the widgets (upper-casing, the spin box's range, leaving a field alone) and saving the
 *        widgets' values, through the dialog's own load/save functions.
 *
 * The file format itself - what a file says and what writing puts in it - is the core's (readFrameSyncFile /
 * writeFrameSyncFile) and is tested without Qt in native/tmdq_core_tests/test_framesyncfile.cpp.
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

    // The dialog's load and save, and the implementations they replaced (kept in the test) compared
    // over generated files.
    void theCoreReadsEveryGeneratedFileLikeTheOldDialogCode();
    void theCoreWritesEveryGeneratedFileLikeTheOldDialogCode();
    void aFileWrittenByTheDialogLoadsBackIntoIt();
};

#endif // TST_FRAMESYNCFILE_H
