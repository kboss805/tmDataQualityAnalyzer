#ifndef TST_MAINVIEWMODEL_HELPERS_H
#define TST_MAINVIEWMODEL_HELPERS_H

#include <QObject>

class TestMainViewModelHelpers : public QObject
{
    Q_OBJECT

private slots:
    void classifyLogMessageSeverity();
    void buildStreamJobAttachesCalibrationByWord();
    // How a stream's configuration becomes the parameters the worker runs on.
    void buildStreamJobDecodesTheSyncPatternAndMask();
    void buildStreamJobDecodesTheSyncPatternAndMask_data();
    void buildStreamJobBuildsTheAcquisitionParameters();
    void buildStreamJobReportsEachWayAConfigIsRejected();
    void buildStreamJobReportsEachWayAConfigIsRejected_data();
    void openFileListsItsChannelsAndOffersThemAsStreams();
    void openFileOnAFileThatCannotBeReadReportsOneErrorAndKeepsNoChannels();
    void openFileShowsANonAsciiChannelNameIntact();
};

#endif // TST_MAINVIEWMODEL_HELPERS_H
