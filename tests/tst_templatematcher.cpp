/**
 * @file tst_templatematcher.cpp
 * @brief Tests for TemplateMatcher (Batch Apply). Pure logic -- runs in CI.
 */

#include "tst_templatematcher.h"

#include <QtTest>

#include "processingtemplate.h"
#include "templatematcher.h"

namespace
{
    ProcessingTemplate makeTemplate(const QVector<int>& channelIds)
    {
        ProcessingTemplate tmpl;
        for (int id : channelIds)
        {
            TemplateStreamEntry entry;
            entry.config.pcmChannelId = id;
            entry.config.label        = QString("Ch %1").arg(id);
            tmpl.entries.append(entry);
        }
        return tmpl;
    }

    QList<QPair<int, QString>> makeFileChannels(const QVector<int>& channelIds)
    {
        QList<QPair<int, QString>> channels;
        for (int id : channelIds)
            channels.append({ id, QString("Ch %1").arg(id) });
        return channels;
    }
}

void TestTemplateMatcher::templateChannelIdsCollectsEveryEntry()
{
    const ProcessingTemplate tmpl = makeTemplate({ 32, 40, 48 });
    const QSet<int> ids = TemplateMatcher::templateChannelIds(tmpl);
    QCOMPARE(ids.size(), 3);
    QVERIFY(ids.contains(32));
    QVERIFY(ids.contains(40));
    QVERIFY(ids.contains(48));
}

void TestTemplateMatcher::matchesWhenChannelSetsIdentical()
{
    const ProcessingTemplate tmpl = makeTemplate({ 32, 40, 48 });
    const auto result = TemplateMatcher::matchFile(tmpl, makeFileChannels({ 32, 40, 48 }));
    QVERIFY(result.ok);
    QVERIFY(result.missing.isEmpty());
    QVERIFY(result.extra.isEmpty());
}

void TestTemplateMatcher::matchesRegardlessOfChannelOrder()
{
    // Exact-SET match: order in the file's channel list is irrelevant.
    const ProcessingTemplate tmpl = makeTemplate({ 32, 40, 48 });
    const auto result = TemplateMatcher::matchFile(tmpl, makeFileChannels({ 48, 32, 40 }));
    QVERIFY(result.ok);
}

void TestTemplateMatcher::rejectsWhenFileMissingAChannel()
{
    const ProcessingTemplate tmpl = makeTemplate({ 32, 40, 48 });
    const auto result = TemplateMatcher::matchFile(tmpl, makeFileChannels({ 32, 40 }));
    QVERIFY(!result.ok);
    QCOMPARE(result.missing, QVector<int>{ 48 });
    QVERIFY(result.extra.isEmpty());
}

void TestTemplateMatcher::rejectsWhenFileHasExtraChannel()
{
    const ProcessingTemplate tmpl = makeTemplate({ 32, 40 });
    const auto result = TemplateMatcher::matchFile(tmpl, makeFileChannels({ 32, 40, 48 }));
    QVERIFY(!result.ok);
    QVERIFY(result.missing.isEmpty());
    QCOMPARE(result.extra, QVector<int>{ 48 });
}

void TestTemplateMatcher::rejectsAndReportsBothMissingAndExtra()
{
    const ProcessingTemplate tmpl = makeTemplate({ 32, 40 });
    const auto result = TemplateMatcher::matchFile(tmpl, makeFileChannels({ 40, 48 }));
    QVERIFY(!result.ok);
    QCOMPARE(result.missing, QVector<int>{ 32 });
    QCOMPARE(result.extra, QVector<int>{ 48 });
}

void TestTemplateMatcher::fileMatchesTemplatePredicateAgrees()
{
    const ProcessingTemplate tmpl = makeTemplate({ 32, 40 });
    QVERIFY(TemplateMatcher::fileMatchesTemplate(tmpl, makeFileChannels({ 32, 40 })));
    QVERIFY(!TemplateMatcher::fileMatchesTemplate(tmpl, makeFileChannels({ 32 })));
}
