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
};

#endif // TST_PLOTCUSTOMIZATIONDIALOG_H
