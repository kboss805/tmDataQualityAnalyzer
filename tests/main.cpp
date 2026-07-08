#include <QApplication>
#include <QFile>
#include <QSettings>
#include <QTextStream>
#include <QtTest>

#include "tomlconfighelper.h"

#include "tst_channeldata.h"
#include "tst_chapter10reader.h"
#include "tst_constants.h"
#include "tst_frameprocessor.h"
#include "tst_framesetup.h"
#include "tst_mainview.h"
#include "tst_mainviewmodel_helpers.h"
#include "tst_plotviewmodel.h"
#include "tst_plotwidget.h"
#include "tst_plotcustomizationdialog.h"
#include "tst_processingcoordinator.h"
#include "tst_stepdetector.h"
#include "tst_calibrationextractor.h"
#include "tst_streamconfigdialog.h"

#include "tst_exportdialog.h"
#include "tst_seriescolumnschema.h"
#include "tst_streamconfigschema.h"

/// Runs a single test suite and appends results to the shared log file.
template<typename T>
int runSuite(const QString& log_path)
{
    // Per-suite temp file for Qt Test output
    QString tmp = log_path + ".tmp";
    QStringList args = { "tests", "-o", tmp + ",txt" };
    QVector<QByteArray> arg_bytes;
    QVector<char*> arg_ptrs;
    for (const QString& a : args)
    {
        arg_bytes.append(a.toLocal8Bit());
        arg_ptrs.append(arg_bytes.last().data());
    }
    int suite_argc = static_cast<int>(arg_ptrs.size());

    T test;
    int result = QTest::qExec(&test, suite_argc, arg_ptrs.data());

    // Append per-suite output to the combined log
    QFile src(tmp);
    if (src.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QFile dst(log_path);
        if (!dst.open(QIODevice::Append | QIODevice::Text))
        {
            return result;
        }
        dst.write(src.readAll());
        src.close();
        src.remove();
    }

    return result;
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // Use separate QSettings scope so tests don't overwrite real app settings
    app.setOrganizationName("tmDataQualityAnalyzer_tests");
    app.setApplicationName("tmDataQualityAnalyzer_tests");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    // Register the custom TOML format so all config file I/O works in tests
    (void)TomlConfigHelper::format();

    // Determine output log path: use -o argument if provided, else output/results.txt
    // output/ is gitignored so results never clutter the repo.
    QString log_path = "output/results.txt";
    for (int i = 1; i < argc; ++i)
    {
        QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "-o" && i + 1 < argc)
        {
            // Strip ",txt" or ",tap" suffix if present
            log_path = QString::fromLocal8Bit(argv[i + 1]).split(',').first();
            break;
        }
    }

    // Clear the log file
    QFile(log_path).remove();

    // Fast mode skips the heavy real-Ch10 integration suites for quick local
    // iteration — enabled by the --fast flag or the TMDQ_FAST_TESTS env var. It is
    // a LOCAL convenience only: CI and the release gate must run the full suite.
    bool fast_mode = qEnvironmentVariableIsSet("TMDQ_FAST_TESTS");
    for (int i = 1; i < argc; ++i)
    {
        if (QString::fromLocal8Bit(argv[i]) == "--fast")
        {
            fast_mode = true;
        }
    }

    int status = 0;

    // Fast suites — pure logic plus widget/dialog construction; no large .ch10
    // fixture, all sub-second. These always run (and are the set CI runs, where the
    // .ch10 fixtures are absent).
    status |= runSuite<TestChannelData>(log_path);
    status |= runSuite<TestConstants>(log_path);
    status |= runSuite<TestMainViewModelHelpers>(log_path);
    status |= runSuite<TestFrameSetup>(log_path);
    status |= runSuite<TestPlotViewModel>(log_path);
    status |= runSuite<TestMainView>(log_path);
    status |= runSuite<TestPlotWidget>(log_path);
    status |= runSuite<TestPlotCustomizationDialog>(log_path);
    status |= runSuite<TestStreamConfigDialog>(log_path);
    status |= runSuite<TestExportDialog>(log_path);
    status |= runSuite<TestStepDetector>(log_path);
    status |= runSuite<TestSeriesColumnSchema>(log_path);
    status |= runSuite<TestStreamConfigSchema>(log_path);

    // Heavy suites — integration over a real .ch10 fixture; TestFrameProcessor alone
    // is ~74 s and dominates the ~85 s run. Skipped in fast mode for quick iteration;
    // CI and the release gate always run them.
    if (!fast_mode)
    {
        status |= runSuite<TestChapter10Reader>(log_path);
        status |= runSuite<TestFrameProcessor>(log_path);
        status |= runSuite<TestProcessingCoordinator>(log_path);
        status |= runSuite<TestCalibrationExtractor>(log_path);
    }
    else
    {
        QTextStream(stdout)
            << "FAST MODE: skipped the real-Ch10 integration suites (TestChapter10Reader, "
               "TestFrameProcessor, TestProcessingCoordinator, TestCalibrationExtractor). "
               "Run without --fast / TMDQ_FAST_TESTS for the full suite.\n";
    }

    return status;
}
