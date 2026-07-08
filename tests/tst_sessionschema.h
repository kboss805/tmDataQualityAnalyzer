/**
 * @file tst_sessionschema.h
 * @brief Unit tests for SessionSchema -- Session <-> JSON round-trip,
 *        schemaVersion rejection, and relative/absolute path resolution
 *        (session save/load, Phase 6, build order §12.2).
 */

#ifndef TST_SESSIONSCHEMA_H
#define TST_SESSIONSCHEMA_H

#include <QObject>

class TestSessionSchema : public QObject
{
    Q_OBJECT

private slots:
    void roundTripTwoSourceMixedModeSession();
    void roundTripPreservesViewStateOverrides();
    void roundTripOmittedOverridesStayUnset();
    void fromJsonWritesCurrentSchemaVersion();
    void fromJsonRejectsNewerSchemaVersion();
    void fromJsonRejectsMissingSchemaVersion();
    void fromJsonRejectsMissingSourcesArray();
    void fromJsonRejectsNonObjectDocument();

    void sessionRelativePathRelativizesUnderSameDrive();
    void sessionRelativePathKeepsAbsoluteWhenOutsideSessionDir();
    void resolveSessionPathResolvesRelativeAgainstSessionDir();
    void resolveSessionPathLeavesAbsoluteUnchanged();
    void relativizeThenResolveRoundTrips();
};

#endif // TST_SESSIONSCHEMA_H
