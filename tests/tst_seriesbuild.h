/**
 * @file tst_seriesbuild.h
 * @brief Unit tests for how processed streams become plot series and share one time axis: the series a
 *        stream yields, their names and indices, the elapsed axis, re-basing across sources, and the order
 *        an imported CSV's streams are given. Pinned through PlotViewModel so the same cases hold on both
 *        sides of the move of that logic into the core (plan step 1.6).
 */

#ifndef TST_SERIESBUILD_H
#define TST_SERIESBUILD_H

#include <QObject>

class TestSeriesBuild : public QObject
{
    Q_OBJECT

private slots:
    void scenariosKeepTheirRecordedBehaviour_data();
    void scenariosKeepTheirRecordedBehaviour();

    // The old view-model code kept in the test as an oracle and compared over generated input, and the
    // core's own interface.
    void theCoreBuildsEveryGeneratedSequenceLikeTheOldViewModelCode();
    void theCoreOrdersImportedStreamsLikeTheOldViewModelCode();
    void theCoreSpecsViewTheProcessedDataWithoutCopyingIt();
    void theTimeBaseReportsHowFarTheSeriesMustShift();
};

#endif // TST_SERIESBUILD_H
