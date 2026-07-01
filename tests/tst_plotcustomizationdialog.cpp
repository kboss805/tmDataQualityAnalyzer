/**
 * @file tst_plotcustomizationdialog.cpp
 * @brief Unit tests for the Customize Plot Series dialog (tree build, tri-state
 *        group toggles, expand/collapse, and the visibility round-trip to the VM).
 *
 * Uses the friend declaration in PlotCustomizationDialog to reach its private
 * widgets/slots (same pattern as TestFrameProcessor).
 */

#include "tst_plotcustomizationdialog.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QtTest>

#include "plotcustomizationdialog.h"
#include "plotviewmodel.h"
#include "processedstreamdata.h"
#include "streamconfig.h"

namespace {

constexpr double kBaseTime = 1000000.0;

/// Lock-only stream (Frame Sync Lock tab): yields a FrameSyncLock + an
/// AccumulatedMissedFrames series, both keyed to streamOrder == pcmId.
ProcessedStreamData makeLockStream(const QString& label, int pcmId, int jobIndex)
{
    ProcessedStreamData d;
    d.streamLabel  = label;
    d.pcmChannelId = pcmId;
    d.jobIndex     = jobIndex;
    d.mode         = StreamMode::FrameSyncLockStats;
    d.timesSec                = { kBaseTime, kBaseTime + 1.0, kBaseTime + 2.0 };
    d.lockPercent             = { 90.0, 95.0, 100.0 };
    d.accumulatedMissedFrames = { 0.0, 1.0, 1.0 };
    return d;
}

/// Receiver-SNR stream: one PlotSeriesData per channel name. The "_RCVR<N>"
/// suffix drives the receiver grouping in the SNR tree.
ProcessedStreamData makeSnrStream(const QString& label, int pcmId, int jobIndex,
                                  const QStringList& channelNames)
{
    ProcessedStreamData d;
    d.streamLabel  = label;
    d.pcmChannelId = pcmId;
    d.jobIndex     = jobIndex;
    d.mode         = StreamMode::ReceiverChannelInfo;
    d.timesSec     = { kBaseTime, kBaseTime + 1.0, kBaseTime + 2.0 };
    for (const QString& name : channelNames)
    {
        ProcessedChannelSeries ch;
        ch.name   = name;
        ch.values = { -80.0, -79.0, -78.0 };
        d.channels.append(ch);
    }
    return d;
}

/// VM with two lock streams (CH 32, CH 33) and one SNR stream (CH 40) whose
/// channels form two receivers: RCVR1 = {L, R}, RCVR2 = {L}.
void populateVm(PlotViewModel& vm)
{
    vm.addStreamData(makeLockStream("Ch 32", 32, 0));
    vm.addStreamData(makeLockStream("Ch 33", 33, 1));
    vm.addStreamData(makeSnrStream("Ch 40", 40, 2, { "L_RCVR1", "R_RCVR1", "L_RCVR2" }));
}

} // namespace

void TestPlotCustomizationDialog::lockTabOneCheckboxPerStream()
{
    PlotViewModel vm;
    populateVm(vm);
    PlotCustomizationDialog dlg(&vm);

    // Two lock streams -> two checkboxes; the default LockPercent view shows the
    // lock series, so both start checked.
    QCOMPARE(dlg.m_lockCheckboxes.size(), 2);
    for (QCheckBox* cb : dlg.m_lockCheckboxes)
        QVERIFY(cb->isChecked());
}

void TestPlotCustomizationDialog::selectAllNoneLock()
{
    PlotViewModel vm;
    populateVm(vm);
    PlotCustomizationDialog dlg(&vm);

    dlg.selectNoneLock();
    for (QCheckBox* cb : dlg.m_lockCheckboxes)
        QVERIFY(!cb->isChecked());

    dlg.selectAllLock();
    for (QCheckBox* cb : dlg.m_lockCheckboxes)
        QVERIFY(cb->isChecked());
}

void TestPlotCustomizationDialog::applyChangesLockVisibility()
{
    PlotViewModel vm;
    populateVm(vm);
    PlotCustomizationDialog dlg(&vm);

    // Uncheck the first lock stream's box, leave the second checked, apply.
    dlg.m_lockCheckboxes[0]->setChecked(false);
    dlg.applyChanges();

    // Box 0's series (both lock and missed) must be hidden.
    for (int idx : dlg.m_lockCheckboxToSeriesIndices.value(dlg.m_lockCheckboxes[0]))
        QVERIFY(!vm.seriesAt(idx).visible);

    // Box 1 is checked with the LockPercent view active: its FrameSyncLock series
    // is visible, but its AccumulatedMissedFrames sibling stays hidden (it belongs
    // to the other axis view).
    for (int idx : dlg.m_lockCheckboxToSeriesIndices.value(dlg.m_lockCheckboxes[1]))
    {
        const PlotSeriesData& s = vm.seriesAt(idx);
        if (s.metricType == PlotSeriesData::MetricType::FrameSyncLock)
            QVERIFY(s.visible);
        else
            QVERIFY(!s.visible);
    }
}

void TestPlotCustomizationDialog::snrTabTreePerStream()
{
    PlotViewModel vm;
    populateVm(vm);
    PlotCustomizationDialog dlg(&vm);

    QCOMPARE(dlg.m_snrStreamCombo->count(), 1);
    const QVector<QTreeWidget*> trees = dlg.m_snrStreamTrees.value(40);
    QVERIFY(!trees.isEmpty());

    int rcvrCount = 0;
    int channelCount = 0;
    for (QTreeWidget* tree : trees)
    {
        for (int r = 0; r < tree->topLevelItemCount(); r++)
        {
            rcvrCount++;
            channelCount += tree->topLevelItem(r)->childCount();
        }
    }
    QCOMPARE(rcvrCount, 2);     // RCVR1, RCVR2
    QCOMPARE(channelCount, 3);  // L,R under RCVR1; L under RCVR2
}

void TestPlotCustomizationDialog::snrTristateCascade()
{
    PlotViewModel vm;
    populateVm(vm);
    PlotCustomizationDialog dlg(&vm);

    QTreeWidget* tree = dlg.m_snrStreamTrees.value(40).first();
    QTreeWidgetItem* rcvr = tree->topLevelItem(0);
    QVERIFY(rcvr->childCount() >= 2);   // RCVR1 has L and R

    // Unchecking the group cascades down to every channel.
    rcvr->setCheckState(0, Qt::Unchecked);
    for (int c = 0; c < rcvr->childCount(); c++)
        QCOMPARE(rcvr->child(c)->checkState(0), Qt::Unchecked);

    // Checking it again cascades back up to fully checked.
    rcvr->setCheckState(0, Qt::Checked);
    for (int c = 0; c < rcvr->childCount(); c++)
        QCOMPARE(rcvr->child(c)->checkState(0), Qt::Checked);

    // Unchecking a single channel drops the group to partially checked.
    rcvr->child(0)->setCheckState(0, Qt::Unchecked);
    QCOMPARE(rcvr->checkState(0), Qt::PartiallyChecked);

    // Unchecking the rest returns it to fully unchecked.
    for (int c = 1; c < rcvr->childCount(); c++)
        rcvr->child(c)->setCheckState(0, Qt::Unchecked);
    QCOMPARE(rcvr->checkState(0), Qt::Unchecked);
}

void TestPlotCustomizationDialog::selectAllNoneSnr()
{
    PlotViewModel vm;
    populateVm(vm);
    PlotCustomizationDialog dlg(&vm);

    const QVector<QTreeWidget*> trees = dlg.m_snrStreamTrees.value(40);

    dlg.selectAllSnr();
    for (QTreeWidget* tree : trees)
        for (int r = 0; r < tree->topLevelItemCount(); r++)
        {
            QTreeWidgetItem* rcvr = tree->topLevelItem(r);
            for (int c = 0; c < rcvr->childCount(); c++)
                QCOMPARE(rcvr->child(c)->checkState(0), Qt::Checked);
        }

    dlg.selectNoneSnr();
    for (QTreeWidget* tree : trees)
        for (int r = 0; r < tree->topLevelItemCount(); r++)
        {
            QTreeWidgetItem* rcvr = tree->topLevelItem(r);
            for (int c = 0; c < rcvr->childCount(); c++)
                QCOMPARE(rcvr->child(c)->checkState(0), Qt::Unchecked);
        }
}

void TestPlotCustomizationDialog::applyChangesSnrVisibility()
{
    PlotViewModel vm;
    populateVm(vm);
    PlotCustomizationDialog dlg(&vm);

    // Uncheck the first channel of RCVR1, leaving the second checked, then apply.
    QTreeWidget* tree = dlg.m_snrStreamTrees.value(40).first();
    QTreeWidgetItem* firstChannel  = tree->topLevelItem(0)->child(0);
    QTreeWidgetItem* secondChannel = tree->topLevelItem(0)->child(1);
    const int hiddenIdx = firstChannel->data(0, Qt::UserRole).toInt();
    const int shownIdx  = secondChannel->data(0, Qt::UserRole).toInt();

    firstChannel->setCheckState(0, Qt::Unchecked);
    dlg.applyChanges();

    QVERIFY(!vm.seriesAt(hiddenIdx).visible);
    QVERIFY(vm.seriesAt(shownIdx).visible);
}

void TestPlotCustomizationDialog::expandCollapseTogglesButton()
{
    PlotViewModel vm;
    populateVm(vm);
    PlotCustomizationDialog dlg(&vm);

    // Trees start collapsed, so the button offers to expand.
    QCOMPARE(dlg.m_snrExpandBtn->text(), QString("Expand All"));

    dlg.toggleExpandCollapseSnr();
    QCOMPARE(dlg.m_snrExpandBtn->text(), QString("Collapse All"));
    for (QTreeWidget* tree : dlg.m_snrStreamTrees.value(40))
        for (int r = 0; r < tree->topLevelItemCount(); r++)
            QVERIFY(tree->topLevelItem(r)->isExpanded());

    dlg.toggleExpandCollapseSnr();
    QCOMPARE(dlg.m_snrExpandBtn->text(), QString("Expand All"));
}

void TestPlotCustomizationDialog::lockRenameAppliesToViewModel()
{
    PlotViewModel vm;
    populateVm(vm);
    PlotCustomizationDialog dlg(&vm);

    QCheckBox* cb = dlg.m_lockCheckboxes.at(0);
    dlg.m_lockNameEdits.value(cb)->setText("Renamed Stream");
    dlg.applyChanges();

    // Applies to every series in the stream (the lock + missed-frames siblings).
    const QVector<int> indices = dlg.m_lockCheckboxToSeriesIndices.value(cb);
    QVERIFY(!indices.isEmpty());
    for (int idx : indices)
        QCOMPARE(vm.seriesAt(idx).name, QString("Renamed Stream"));
}

void TestPlotCustomizationDialog::lockRecolorAppliesToViewModel()
{
    PlotViewModel vm;
    populateVm(vm);
    PlotCustomizationDialog dlg(&vm);

    QCheckBox* cb = dlg.m_lockCheckboxes.at(0);
    dlg.m_lockColors[cb] = QColor(Qt::magenta); // simulate a swatch color pick
    dlg.applyChanges();

    const QVector<int> indices = dlg.m_lockCheckboxToSeriesIndices.value(cb);
    QVERIFY(!indices.isEmpty());
    for (int idx : indices)
        QCOMPARE(vm.seriesAt(idx).color, QColor(Qt::magenta));
}

void TestPlotCustomizationDialog::snrRenameRecolorAppliesToViewModel()
{
    PlotViewModel vm;
    populateVm(vm);
    PlotCustomizationDialog dlg(&vm);

    QTreeWidget* tree = dlg.m_snrStreamTrees.value(40).first();
    QTreeWidgetItem* leaf = tree->topLevelItem(0)->child(0);
    const int idx = leaf->data(0, Qt::UserRole).toInt();

    // The channel context menu stores pending edits in these item roles
    // (UserRole+1 name, UserRole+2 color); applyChanges() pushes them to the VM.
    leaf->setData(0, Qt::UserRole + 1, QString("Custom SNR"));
    leaf->setData(0, Qt::UserRole + 2, QColor(Qt::cyan));
    dlg.applyChanges();

    QCOMPARE(vm.seriesAt(idx).name, QString("Custom SNR"));
    QCOMPARE(vm.seriesAt(idx).color, QColor(Qt::cyan));
}

void TestPlotCustomizationDialog::applyChangesEmitsAppearanceSignal()
{
    PlotViewModel vm;
    populateVm(vm);
    PlotCustomizationDialog dlg(&vm);

    QSignalSpy spy(&vm, &PlotViewModel::seriesAppearanceChanged);
    dlg.applyChanges();
    QCOMPARE(spy.count(), 1); // batched: exactly one refresh notification per apply
}
