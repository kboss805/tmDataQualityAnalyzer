/**
 * @file tst_streamjob.h
 * @brief Unit tests for what MainViewModel::buildStreamJob does around the core's job builder: it carries the
 *        user's Qt settings across, reads the application root and byte order it holds, and reports what the
 *        builder found as a QString and as signals.
 *
 * The builder itself - the validation, the frame-parameter table and its calibration (plan step 1.5) - is the
 * core's and is tested without Qt in native/tmdq_core_tests/test_streamjob.cpp.
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
};

#endif // TST_STREAMJOB_H
