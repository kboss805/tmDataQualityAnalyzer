#ifndef TST_BATCHCONTROLLER_H
#define TST_BATCHCONTROLLER_H

#include <QObject>

/// @brief Unit tests for BatchController, the Apply Template batch loop (US1.1).
///
/// The appearance-capture and appearance-reuse cases moved here from
/// TestMainView when the state machine left the View: they are about the batch,
/// not about the window, and they no longer need a MainView to run. The
/// queue-advance cases are new - they were impractical while the loop's state
/// lived in MainView's private members.
class TestBatchController : public QObject
{
    Q_OBJECT

private slots:
    void buildTemplateCapturesConfigsAndAppearance();
    void applyAppearanceRenamesMatchingSeries();
    void missingFilesAreSkippedAndReported();
    void startWithNoFilesDoesNothing();
};

#endif // TST_BATCHCONTROLLER_H
