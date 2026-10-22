/**
 * @file tst_templatematcher.cpp
 * @brief Tests for TemplateMatcher (Batch Apply). Pure logic -- runs in CI.
 */

#include "tst_templatematcher.h"

#include <algorithm>
#include <limits>

#include <QSet>
#include <QtTest>

#include "batchcontroller.h"
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

    /// What a match says, in one plain type. Every test reads the matcher through matchFile() below, so
    /// moving the matcher (de-Qt, plan step 1.3) changes that one function and no assertion.
    struct Match
    {
        bool ok = false;
        QVector<int> missing;
        QVector<int> extra;
    };

    Match matchFile(const ProcessingTemplate& tmpl, const QList<QPair<int, QString>>& fileChannels)
    {
        std::vector<std::pair<int, std::string>> channels;
        for (const QPair<int, QString>& ch : fileChannels)
            channels.push_back({ ch.first, ch.second.toStdString() });
        const TemplateMatcher::MatchResult result = BatchController::matchFile(tmpl, channels);
        return { result.ok, QVector<int>(result.missing.begin(), result.missing.end()),
                 QVector<int>(result.extra.begin(), result.extra.end()) };
    }

    Match matchIds(const QVector<int>& templateIds, const QVector<int>& fileIds)
    {
        return matchFile(makeTemplate(templateIds), makeFileChannels(fileIds));
    }
}

void TestTemplateMatcher::everyTemplateEntryCountsAsAChannel()
{
    // A file with no channels at all lacks every channel the template configures.
    const Match result = matchIds({ 48, 32, 40 }, {});
    QVERIFY(!result.ok);
    QCOMPARE(result.missing, (QVector<int>{ 32, 40, 48 }));
    QVERIFY(result.extra.isEmpty());
}

void TestTemplateMatcher::matchesWhenChannelSetsIdentical()
{
    const Match result = matchIds({ 32, 40, 48 }, { 32, 40, 48 });
    QVERIFY(result.ok);
    QVERIFY(result.missing.isEmpty());
    QVERIFY(result.extra.isEmpty());
}

void TestTemplateMatcher::matchesRegardlessOfChannelOrder()
{
    // Exact-SET match: order in the file's channel list is irrelevant.
    QVERIFY(matchIds({ 32, 40, 48 }, { 48, 32, 40 }).ok);
}

void TestTemplateMatcher::rejectsWhenFileMissingAChannel()
{
    const Match result = matchIds({ 32, 40, 48 }, { 32, 40 });
    QVERIFY(!result.ok);
    QCOMPARE(result.missing, QVector<int>{ 48 });
    QVERIFY(result.extra.isEmpty());
}

void TestTemplateMatcher::rejectsWhenFileHasExtraChannel()
{
    const Match result = matchIds({ 32, 40 }, { 32, 40, 48 });
    QVERIFY(!result.ok);
    QVERIFY(result.missing.isEmpty());
    QCOMPARE(result.extra, QVector<int>{ 48 });
}

void TestTemplateMatcher::rejectsAndReportsBothMissingAndExtra()
{
    const Match result = matchIds({ 32, 40 }, { 40, 48 });
    QVERIFY(!result.ok);
    QCOMPARE(result.missing, QVector<int>{ 32 });
    QCOMPARE(result.extra, QVector<int>{ 48 });
}

void TestTemplateMatcher::okMeansNothingMissingAndNothingExtra()
{
    QVERIFY(matchIds({ 32, 40 }, { 32, 40 }).ok);
    QVERIFY(!matchIds({ 32, 40 }, { 32 }).ok);
    QVERIFY(!matchIds({ 32 }, { 32, 40 }).ok);
    QVERIFY(!matchIds({ 32, 40 }, { 40, 48 }).ok);
}

void TestTemplateMatcher::aTemplateAndAFileWithNoChannelsMatch()
{
    // Vacuously the same set. A caller that must refuse an empty template has to say so itself.
    const Match result = matchIds({}, {});
    QVERIFY(result.ok);
    QVERIFY(result.missing.isEmpty());
    QVERIFY(result.extra.isEmpty());
}

void TestTemplateMatcher::anEmptyTemplateFindsEveryFileChannelExtra()
{
    const Match result = matchIds({}, { 40, 32 });
    QVERIFY(!result.ok);
    QVERIFY(result.missing.isEmpty());
    QCOMPARE(result.extra, (QVector<int>{ 32, 40 }));
}

void TestTemplateMatcher::repeatedChannelsCountOnce()
{
    // Two entries for one channel, or a file listing a channel twice, are still one channel.
    QVERIFY(matchIds({ 32, 32, 40 }, { 32, 40 }).ok);
    QVERIFY(matchIds({ 32, 40 }, { 32, 40, 40, 32 }).ok);

    const Match extra = matchIds({ 32 }, { 40, 40, 32 });
    QCOMPARE(extra.extra, QVector<int>{ 40 });   // reported once
    const Match missing = matchIds({ 40, 40, 32 }, { 32 });
    QCOMPARE(missing.missing, QVector<int>{ 40 });
}

void TestTemplateMatcher::missingAndExtraComeBackSorted()
{
    const Match result = matchIds({ 50, 10, 30, 20, 40, 7 }, { 35, 5, 25, 15, 7 });
    QCOMPARE(result.missing, (QVector<int>{ 10, 20, 30, 40, 50 }));
    QCOMPARE(result.extra, (QVector<int>{ 5, 15, 25, 35 }));
}

void TestTemplateMatcher::anyIntegerCanBeAChannelId()
{
    constexpr int kMin = std::numeric_limits<int>::min();
    constexpr int kMax = std::numeric_limits<int>::max();
    const Match result = matchIds({ kMax, -1, 0, kMin }, { 0, kMax, 77 });
    QCOMPARE(result.missing, (QVector<int>{ kMin, -1 }));
    QCOMPARE(result.extra, QVector<int>{ 77 });
}

void TestTemplateMatcher::channelNamesPlayNoPart()
{
    // The key is the id; a file may name the same channel differently.
    QList<QPair<int, QString>> renamed;
    renamed.append({ 32, QString("Something else") });
    renamed.append({ 40, QString() });
    QVERIFY(matchFile(makeTemplate({ 32, 40 }), renamed).ok);
}

// ---------------------------------------------------------------------------
// The implementation the core replaced, kept as an oracle (verbatim apart from its names and the
// type it hands back), compared over generated input.
// ---------------------------------------------------------------------------

namespace
{
    namespace oracle
    {
        QSet<int> templateChannelIds(const ProcessingTemplate& tmpl)
        {
            QSet<int> ids;
            for (const TemplateStreamEntry& entry : tmpl.entries)
                ids.insert(entry.config.pcmChannelId);
            return ids;
        }

        Match matchFile(const ProcessingTemplate& tmpl, const QList<QPair<int, QString>>& fileChannels)
        {
            const QSet<int> templateIds = templateChannelIds(tmpl);

            QSet<int> fileIds;
            for (const QPair<int, QString>& ch : fileChannels)
                fileIds.insert(ch.first);

            Match result;
            for (int id : templateIds)
            {
                if (!fileIds.contains(id))
                    result.missing.append(id);
            }
            for (int id : fileIds)
            {
                if (!templateIds.contains(id))
                    result.extra.append(id);
            }

            std::sort(result.missing.begin(), result.missing.end());
            std::sort(result.extra.begin(), result.extra.end());
            result.ok = result.missing.isEmpty() && result.extra.isEmpty();
            return result;
        }
    }
}

void TestTemplateMatcher::matchesLikeTheQSetImplementationOverGeneratedInputs()
{
    quint32 state = 20261025u;
    auto next = [&state]() { state = state * 1664525u + 1013904223u; return state >> 8; };
    auto below = [&](int n) { return int(next() % quint32(n)); };

    auto join = [](const QVector<int>& ids) {
        QStringList parts;
        for (int id : ids)
            parts << QString::number(id);
        return parts.join(',');
    };

    // Ids from a small pool so the sets overlap, plus the awkward ones.
    const QVector<int> pool = { -1, 0, 1, 2, 3, 5, 8, 13, 21, 32, 33, 40, 48, 64, 255, 1000,
                                std::numeric_limits<int>::min(), std::numeric_limits<int>::max() };
    auto draw = [&]() {
        QVector<int> ids;
        const int n = below(9);
        for (int i = 0; i < n; ++i)
            ids.append(pool[below(int(pool.size()))]);
        return ids;
    };

    int same = 0, only_missing = 0, only_extra = 0, both = 0, with_repeats = 0;
    for (int i = 0; i < 4000; ++i)
    {
        const QVector<int> template_ids = draw();
        QVector<int> file_ids;
        switch (below(4))
        {
        case 0:   // the same set, reshuffled and with repeats
            file_ids = template_ids;
            for (int k = 0; k < int(file_ids.size()); ++k)
                std::swap(file_ids[k], file_ids[below(int(file_ids.size()))]);
            if (!file_ids.isEmpty() && below(2))
                file_ids.append(file_ids[below(int(file_ids.size()))]);
            break;
        case 1:   // the template's channels less some
            for (int id : template_ids)
                if (below(3))
                    file_ids.append(id);
            break;
        case 2:   // the template's channels plus some
            file_ids = template_ids;
            file_ids += draw();
            break;
        default:  // unrelated
            file_ids = draw();
            break;
        }

        const ProcessingTemplate tmpl = makeTemplate(template_ids);
        const QList<QPair<int, QString>> channels = makeFileChannels(file_ids);
        const Match want = oracle::matchFile(tmpl, channels);
        const Match got = matchFile(tmpl, channels);
        QVERIFY2(got.ok == want.ok && got.missing == want.missing && got.extra == want.extra,
                 qPrintable(QString("template %1 file %2").arg(join(template_ids), join(file_ids))));

        same += want.ok;
        only_missing += !want.missing.isEmpty() && want.extra.isEmpty();
        only_extra += want.missing.isEmpty() && !want.extra.isEmpty();
        both += !want.missing.isEmpty() && !want.extra.isEmpty();
        with_repeats += QSet<int>(template_ids.begin(), template_ids.end()).size() != template_ids.size();
    }
    // The generator must reach every outcome in volume, or "no difference" proves nothing.
    QVERIFY2(same > 500, qPrintable(QString::number(same)));
    QVERIFY2(only_missing > 500, qPrintable(QString::number(only_missing)));
    QVERIFY2(only_extra > 500, qPrintable(QString::number(only_extra)));
    QVERIFY2(both > 500, qPrintable(QString::number(both)));
    QVERIFY2(with_repeats > 500, qPrintable(QString::number(with_repeats)));
}
