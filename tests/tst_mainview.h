/**
 * @file tst_mainview.h
 * @brief Smoke tests for MainView — construction, destruction, log helpers.
 */

#ifndef TST_MAINVIEW_H
#define TST_MAINVIEW_H

#include <QObject>

class TestMainView : public QObject
{
    Q_OBJECT

private slots:
    void constructsAndDestroysWithoutCrash();
    void windowTitleIsNonEmpty();
    void titleBarExists();

    // US6.3 — CSV import / file routing
    void supportedFileDetection();
    void openPathImportsCsv();
    void importValidCsvPopulatesPlot();
    void importInvalidCsvIsRejected();
    void batchReapplyAppearanceRenamesMatchingSeries();
    void batchBuildTemplateCapturesConfigsAndAppearance();

    // Optional full manual: absence is a normal state, not an error.
    void fullManualPathEmptyWhenNotInstalled();
    void fullManualPathFoundWhenInstalled();
    void helpSubmenuHoldsManualAndAbout();
};

#endif // TST_MAINVIEW_H
