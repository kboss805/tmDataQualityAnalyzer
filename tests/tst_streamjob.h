/**
 * @file tst_streamjob.h
 * @brief Unit tests for turning a stream's configuration into a processing job (validation, the
 *        frame-parameter table and its calibration), pinned through MainViewModel::buildStreamJob so
 *        the same cases hold on both sides of the move into the core (plan step 1.5).
 */

#ifndef TST_STREAMJOB_H
#define TST_STREAMJOB_H

#include <QObject>
#include <QString>

class MainViewModel;
struct ProcessingParams;
struct StreamConfig;
struct StreamJob;

class TestStreamJob : public QObject
{
    Q_OBJECT

public:
    /// MainViewModel keeps buildStreamJob() and the application root private and befriends this class
    /// alone, so the helpers the cases share reach them through these.
    static bool buildStreamJob(MainViewModel& vm, const StreamConfig& cfg, const ProcessingParams& base,
                               StreamJob& job, QString& error);
    static void setAppRoot(MainViewModel& vm, const QString& root);

private slots:
    void jobsKeepTheirRecordedBehaviour_data();
    void jobsKeepTheirRecordedBehaviour();

    // The old view-model code kept in the test as an oracle and compared over generated configurations,
    // and the core's own interface.
    void theCoreBuildsEveryGeneratedJobLikeTheOldViewModelCode();
    void theCoreReadsTheDefaultFileUnderANonAsciiApplicationRoot();
    void theCoreReportsErrorsAndLogLinesAsPlainText();
};

#endif // TST_STREAMJOB_H
