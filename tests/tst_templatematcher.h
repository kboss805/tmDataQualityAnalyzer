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
    void everyTemplateEntryCountsAsAChannel();
    void matchesWhenChannelSetsIdentical();
    void matchesRegardlessOfChannelOrder();
    void rejectsWhenFileMissingAChannel();
    void rejectsWhenFileHasExtraChannel();
    void rejectsAndReportsBothMissingAndExtra();
    void okMeansNothingMissingAndNothingExtra();

    // Edge cases pinned against the QSet implementation BEFORE it moved to the core (de-Qt, plan
    // step 1.3).
    void aTemplateAndAFileWithNoChannelsMatch();
    void anEmptyTemplateFindsEveryFileChannelExtra();
    void repeatedChannelsCountOnce();
    void missingAndExtraComeBackSorted();
    void anyIntegerCanBeAChannelId();
    void channelNamesPlayNoPart();

    // The implementation the core replaced, kept in the test and compared over generated input.
    void matchesLikeTheQSetImplementationOverGeneratedInputs();
};

#endif // TST_TEMPLATEMATCHER_H
