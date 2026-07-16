/**
 * @file tst_templatematcher.h
 * @brief Tests for TemplateMatcher exact-set channel-ID validation (Batch Apply).
 */

#ifndef TST_TEMPLATEMATCHER_H
#define TST_TEMPLATEMATCHER_H

#include <QObject>

class TestTemplateMatcher : public QObject
{
    Q_OBJECT

private slots:
    void templateChannelIdsCollectsEveryEntry();
    void matchesWhenChannelSetsIdentical();
    void matchesRegardlessOfChannelOrder();
    void rejectsWhenFileMissingAChannel();
    void rejectsWhenFileHasExtraChannel();
    void rejectsAndReportsBothMissingAndExtra();
    void fileMatchesTemplatePredicateAgrees();
};

#endif // TST_TEMPLATEMATCHER_H
