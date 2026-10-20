/**
 * @file tst_tomldocument.h
 * @brief Tests for TomlDocument, the reader/writer behind every settings file.
 *
 * TomlDocument had no suite of its own: it was exercised only through FrameSetup and the
 * frame-sync dialog helpers. These cases pin its behaviour directly, including the parts
 * Qt supplied for free - BOM and UTF-16 detection, whitespace and '+' tolerance in
 * numbers, the %g double format, CRLF on write - because those are exactly what a port
 * off QString would change without any caller noticing.
 */

#ifndef TST_TOMLDOCUMENT_H
#define TST_TOMLDOCUMENT_H

#include <QObject>

class TestTomlDocument : public QObject
{
    Q_OBJECT

private slots:
    void readsSectionsAndKeysInFileOrder();
    void doubleBracketHeaderOpensAnArrayTableNotASection();
    void arrayTablesKeepFileOrderKeysAndValuesAsWritten();
    void arrayTableNamesMatchWithoutCaseButOtherwiseExactly();
    void aMalformedArrayHeaderOpensAnEntryNoNameMatches();
    void aSectionHeaderEndsAnEntryAndSectionsAreUnaffectedByEntries();
    void commentsAreQuoteAwareAndEscapesUnescape();
    void repeatedKeyOverwrites();
    void readsUtf8BomAndCrlf();
    void readsUtf16WithBom_data();
    void readsUtf16WithBom();
    void nonAsciiValuesAndPathsRoundTrip();
    void integerParsing();
    void numberParsing();
    void booleanRule();
    void writesTheExactFileFormat();
    void doublesAreWrittenAsSixSignificantDigits();
    void saveTruncatesAnExistingFile();
    void missingFileAndUnwritablePathReportErrors();
};

#endif // TST_TOMLDOCUMENT_H
