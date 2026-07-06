#ifndef TST_PLOTCUSTOMIZATIONDIALOG_H
#define TST_PLOTCUSTOMIZATIONDIALOG_H

#include <QObject>

class TestPlotCustomizationDialog : public QObject
{
    Q_OBJECT

private slots:
    void lockTabOneCheckboxPerStream();
    void selectAllNoneLock();
    void applyChangesLockVisibility();
    void snrTabTreePerStream();
    void snrTristateCascade();
    void selectAllNoneSnr();
    void applyChangesSnrVisibility();
    void expandCollapseTogglesButton();
    void lockRenameAppliesToViewModel();
    void lockRecolorAppliesToViewModel();
    void snrRenameRecolorAppliesToViewModel();
    void applyChangesEmitsAppearanceSignal();

    // Regression: the dialog captures stable series ids, so applyChanges() stays
    // correct (and never reads out of range) if the series list is reordered or a
    // captured series is removed while the modal dialog is open (async reprocess).
    void applyChangesRobustToSeriesListChange();
};

#endif // TST_PLOTCUSTOMIZATIONDIALOG_H
