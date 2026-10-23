/**
 * @file tst_coreqtfree.h
 * @brief Keeps the processing core free of Qt, so the claim stays true as the code changes.
 *
 * The de-Qt sweep took Qt out of the decode pipeline one class at a time, each step proved by
 * tests that pass on the Qt version first. What those tests cannot prove is the property the
 * whole sweep is for: that the core COMPILES with no Qt on the include path, which is what lets it
 * be built as a framework-free DLL behind a flat C ABI (the exploration on
 * `explore/dotnet-migration`). One stray `#include <QString>` in a shared header breaks that and
 * nothing else notices - this was found exactly that way, in `constants.h`, by trying to build the
 * core with no Qt at all.
 *
 * So this suite walks the project-local `#include` closure of every core source file and fails on
 * any Qt header, and on any reach into the ViewModel or View layers. That is the same thing a
 * compiler with no Qt include path would object to, without needing one.
 */

#ifndef TST_CORE_QT_FREE_H
#define TST_CORE_QT_FREE_H

#include <QObject>

class TestCoreQtFree : public QObject
{
    Q_OBJECT

private slots:
    void theProcessingCoreIncludesNoQtHeader();
    void theProcessingCoreDoesNotReachIntoTheViewModelOrView();
    void everyModelSourceIsClassifiedAsCoreOrUiSession();
    void theGuardSeesQtThroughSharedHeaders();

    // The end state of stage 1 of the Avalonia plan: outside the View, Qt is confined to a named list.
    void outsideTheViewQtIsConfinedToTheListedFiles();
    void everyOtherHeaderIsQtFreeThroughAndThrough();
    void theConfinementGuardSeesAnUnlistedAndAStaleFile();

    // The MSBuild project that builds tmdq_core.dll (plan step 2.1) compiles the same core as qmake does.
    void theNativeProjectCompilesExactlyTheCoreSources();
    void theNativeProjectGuardReadsProjectFilesCorrectly();
    void theNativeProjectGuardSeesEveryKindOfDrift();
    void theAbiSourcesIncludeNoQtAndReachNoHigherLayer();
    void theAbiGuardSeesQtAndHigherLayers();
};

#endif // TST_CORE_QT_FREE_H
