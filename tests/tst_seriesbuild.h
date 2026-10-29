/**
 * @file tst_seriesbuild.h
 * @brief Unit tests for what the PlotViewModel does with processed streams and imported files: dropping a stream's
 *        earlier series, judging overlap, moving the shared time base, ordering the legend, removing a source and taking
 *        an import in, pinned as one digest of its data layer per scenario.
 *
 * The functions behind it - the series a stream yields, the time base, the order an import's streams are given - are the
 * core's, and every scenario and the generated sequences are tested on them without Qt in
 * native/tmdq_core_tests/test_seriesbuild.cpp.
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
};

#endif // TST_SERIESBUILD_H
