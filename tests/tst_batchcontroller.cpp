#include "tst_batchcontroller.h"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSignalSpy>
#include <QtTest>

#include "batchcontroller.h"
#include "mainviewmodel.h"
#include "plotviewmodel.h"
#include "processedstreamdata.h"
#include "processingtemplate.h"
#include "source.h"
#include "streamconfig.h"

namespace {

/// One frame-sync stream's worth of plotted data, so the appearance cases have
/// real series to match against.
void addLockSeries(PlotViewModel& plot, int pcmChannelId, int sourceId)
{
    ProcessedStreamData d;
    d.streamLabel             = QString("Ch %1").arg(pcmChannelId).toStdString();
    d.pcmChannelId            = pcmChannelId;
    d.sourceId                = sourceId;
    d.mode                    = StreamMode::FrameSyncLockStats;
    d.timesSec                = { 0.0, 1.0, 2.0 };
    d.lockPercent             = { 90.0, 95.0, 100.0 };
    d.accumulatedMissedFrames = { 0.0, 1.0, 1.0 };
    plot.addStreamData(d);
}

} // namespace

void TestBatchController::buildTemplateCapturesConfigsAndAppearance()
{
    // Save as Template: the capture must carry the source's stream configs and
    // time channel, and snapshot each stream's current series appearance
    // (name/color) keyed by metric/receiver/channel.
    PlotViewModel plot;
    addLockSeries(plot, /*pcmChannelId=*/5, /*sourceId=*/0);

    int lock_id = -1;
    for (const PlotSeriesData& s : plot.allSeries())
    {
        if (s.metricType == PlotSeriesData::MetricType::FrameSyncLock)
        {
            lock_id = s.id;
        }
    }
    QVERIFY(lock_id >= 0);
    plot.renameSeriesById(lock_id, "MyLock");
    plot.recolorSeriesById(lock_id, QColor("#abcdef"));
    plot.commitAppearanceChanges();

    Source src;
    src.sourceId         = 0;
    src.timeChannelIndex = 2;
    src.swapBytes        = false;
    StreamConfig cfg;
    cfg.pcmChannelId = 5;
    cfg.label        = "Ch 5";
    cfg.process      = true;
    cfg.mode         = StreamMode::FrameSyncLockStats;
    src.streamConfigs.append(cfg);

    const ProcessingTemplate tmpl = BatchController::buildTemplate(src, plot);

    QCOMPARE(tmpl.timeChannelIndex, 2);
    QCOMPARE(tmpl.swapBytes, false);
    QCOMPARE(tmpl.entries.size(), 1);
    QCOMPARE(tmpl.entries[0].config.pcmChannelId, 5);

    bool found_lock_appearance = false;
    for (const SeriesAppearance& a : tmpl.entries[0].appearance)
    {
        if (a.metricType == PlotSeriesData::MetricType::FrameSyncLock)
        {
            QCOMPARE(a.name, QString("MyLock"));
            QCOMPARE(a.color, QColor("#abcdef"));
            found_lock_appearance = true;
        }
    }
    QVERIFY(found_lock_appearance);
}

void TestBatchController::applyAppearanceRenamesMatchingSeries()
{
    // After a batch file finishes, the template's captured name/color for each
    // stream is re-applied onto that file's freshly-created series, matched by
    // (channel id, metric, receiver, channel).
    PlotViewModel plot;
    addLockSeries(plot, /*pcmChannelId=*/5, /*sourceId=*/0);

    ProcessingTemplate tmpl;
    TemplateStreamEntry entry;
    entry.config.pcmChannelId = 5;
    SeriesAppearance appearance;
    appearance.metricType    = PlotSeriesData::MetricType::FrameSyncLock;
    appearance.receiverIndex = 0;
    appearance.channelIndex  = 0;
    appearance.name          = "Custom Lock";
    appearance.color         = QColor("#123456");
    entry.appearance.append(appearance);
    tmpl.entries.append(entry);

    BatchController::applyAppearance(tmpl, /*sourceId=*/0, plot);

    bool found = false;
    for (const PlotSeriesData& s : plot.allSeries())
    {
        if (s.metricType == PlotSeriesData::MetricType::FrameSyncLock && s.streamOrder == 5)
        {
            QCOMPARE(s.name, QString("Custom Lock"));
            QCOMPARE(s.color, QColor("#123456"));
            found = true;
        }
    }
    QVERIFY(found);
}

void TestBatchController::validateComparesEachFilesChannelsToTheTemplate()
{
    // The up-front check reads each file's PCM channel list and compares its id set to the
    // template's: an exact match passes, and a mismatch is rejected with the ids that differ.
    QDir dir(QCoreApplication::applicationDirPath());
    dir.cdUp();
    const QString prn = dir.filePath("data/test files/prn_testfile, sample.ch10");   // channels 13..16
    if (!QFileInfo::exists(prn))
        QSKIP("prn sample fixture not available");

    auto templateOf = [](const QList<int>& ids) {
        ProcessingTemplate tmpl;
        for (int id : ids)
        {
            TemplateStreamEntry entry;
            entry.config.pcmChannelId = id;
            tmpl.entries.append(entry);
        }
        return tmpl;
    };
    const QString gone = QDir::tempPath() + "/tst_validate_missing.ch10";
    QVERIFY(!QFileInfo::exists(gone));

    const QVector<BatchController::FileCheck> exact = BatchController::validate(templateOf({13, 14, 15, 16}), {prn});
    QCOMPARE(exact.size(), 1);
    QVERIFY(exact[0].ok);
    QVERIFY(exact[0].reason.isEmpty());
    QCOMPARE(exact[0].filepath, prn);

    const QVector<BatchController::FileCheck> fewer = BatchController::validate(templateOf({13, 14}), {prn});
    QVERIFY(!fewer[0].ok);
    QCOMPARE(fewer[0].reason, QString("extra channel(s): 15, 16"));

    const QVector<BatchController::FileCheck> more = BatchController::validate(templateOf({13, 14, 15, 16, 17}), {prn});
    QVERIFY(!more[0].ok);
    QCOMPARE(more[0].reason, QString("missing channel(s): 17"));

    const QVector<BatchController::FileCheck> unreadable = BatchController::validate(templateOf({13}), {gone, prn});
    QCOMPARE(unreadable.size(), 2);
    QVERIFY(!unreadable[0].ok);
    QCOMPARE(unreadable[0].reason, QString("could not read channels"));
    QVERIFY(!unreadable[1].ok);   // 13 alone is not the file's whole set
}

void TestBatchController::missingFilesAreSkippedAndReported()
{
    // A queued file that is gone by the time its turn comes is skipped rather
    // than stalling the run, and the summary counts it. Every file here is
    // missing, so the loop drains without ever opening a reader - which is what
    // makes the state machine testable at all now that it is not in the View.
    MainViewModel view_model;
    PlotViewModel plot;
    BatchController batch(&view_model, &plot);

    QSignalSpy finished(&batch, &BatchController::finished);
    QSignalSpy messages(&batch, &BatchController::message);

    ProcessingTemplate tmpl;
    TemplateStreamEntry entry;
    entry.config.pcmChannelId = 5;
    tmpl.entries.append(entry);

    const QStringList gone = { QDir::tempPath() + "/tst_batch_missing_a.ch10",
                               QDir::tempPath() + "/tst_batch_missing_b.ch10" };
    for (const QString& path : gone)
    {
        QVERIFY(!QFileInfo::exists(path));
    }

    batch.start(tmpl, gone, BatchController::Options{});

    QCOMPARE(finished.count(), 1);
    QVERIFY(!batch.active());   // the run is over, not stuck waiting on a reader

    // One warning per missing file, and a summary that counts both as skipped.
    int warnings = 0;
    QString summary;
    for (const QList<QVariant>& call : messages)
    {
        const auto level = call.at(0).value<MainViewModel::LogLevel>();
        const QString text = call.at(1).toString();
        if (level == MainViewModel::LogLevel::Warning)
        {
            ++warnings;
        }
        if (text.startsWith("Batch complete"))
        {
            summary = text;
        }
    }
    QCOMPARE(warnings, 2);
    QVERIFY2(summary.contains("0 processed") && summary.contains("2 skipped"), qPrintable(summary));
}

void TestBatchController::startWithNoFilesDoesNothing()
{
    // Nothing matched the template, so there is no run: no session is cleared and
    // no completion is announced.
    MainViewModel view_model;
    PlotViewModel plot;
    BatchController batch(&view_model, &plot);

    QSignalSpy finished(&batch, &BatchController::finished);
    QSignalSpy messages(&batch, &BatchController::message);

    batch.start(ProcessingTemplate{}, QStringList{}, BatchController::Options{});

    QVERIFY(!batch.active());
    QCOMPARE(finished.count(), 0);
    QCOMPARE(messages.count(), 0);
}
