/**
 * @file tst_seriescolumnschema.h
 * @brief Unit tests for SeriesColumnSchema — the CSV column-header format and its
 *        inverse parser, pinned as a round-trip so export and import can't drift.
 */

#ifndef TST_SERIESCOLUMNSCHEMA_H
#define TST_SERIESCOLUMNSCHEMA_H

#include <QObject>

class TestSeriesColumnSchema : public QObject
{
    Q_OBJECT

private slots:
    void snrSeriesNameFormat();
    void columnHeaderAddsMetricSuffix();
    void columnHeaderSnrPassthrough();
    void parseLockHeader();
    void parseMissedHeader();
    void parseSnrHeader();
    void parseSnrLabelWithSpaces();
    void parseSnrShapeWinsOverSuffix();
    void parseUnknownFallsBackToSnr();
    void roundTripLockMissedSnr();

    // Source qualifier (Phase 1 multi-file input, §7): source 0 stays byte-identical
    // to the pre-multi-file format; sources 1+ get a leading "S<n>| " qualifier that
    // parseColumnHeader() strips back off before recovering identity.
    void columnHeaderSourceZeroIsUnqualified();
    void columnHeaderNonZeroSourceAddsQualifier();
    void parseSourceQualifiedSnrHeader();
    void parseSourceQualifiedLockHeader();
    void parseUnqualifiedHeaderDefaultsToSourceZero();
    void headerStartingWithLetterSButNotAQualifierIsUnaffected();
    void roundTripPreservesSourceId();

    // Edge cases pinned against the QString implementation BEFORE it moved to std::string (de-Qt,
    // plan step 1.1): what a hand-edited or third-party CSV header resolves to.
    void parseEdgeCasesKeepTheirRecordedBehaviour_data();
    void parseEdgeCasesKeepTheirRecordedBehaviour();
    void formatEdgeCasesKeepTheirRecordedBehaviour_data();
    void formatEdgeCasesKeepTheirRecordedBehaviour();

    // The Qt-free implementation against the QString one it replaced, kept in the test as an oracle.
    void theStdImplementationReadsEveryGeneratedHeaderLikeTheQStringOne();
    void theStdFormattersAgreeWithTheQStringOnes();
    void aNameThatLooksLikeASourceQualifierDoesNotRoundTrip();
};

#endif // TST_SERIESCOLUMNSCHEMA_H
