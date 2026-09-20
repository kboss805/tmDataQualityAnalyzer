#ifndef TST_FRAMESETUP_H
#define TST_FRAMESETUP_H

#include <QObject>

class TestFrameSetup : public QObject
{
    Q_OBJECT

private slots:
    void defaultLengthIsZero();
    void clearParametersResetsToEmpty();
    void getParameterInvalidIndexReturnsNull();
    void getParameterNegativeIndexReturnsNull();
    void tryLoadingFileValidFile();
    void tryLoadingFileMissingWordKey();
    void tryLoadingFileOutOfBoundsWord();
    void tryLoadingFileWordZeroFails();
    void tryLoadingFileWordEqualsFrameSize();
    void saveToSettingsWritesCorrectData();

    // Dynamic frame size tests
    void tryLoadingFileSmallerFrameAccepts();
    void tryLoadingFileSmallerFrameRejectsBoundary();
    void tryLoadingFileLargerFrameAccepts();
    void tryLoadingFileSingleChannelFrameSize();
    void receiverIndexFromNameInvertsParameterName();

    void wordsInMinorFrameRoundsUp();
    void tryLoadingFileMetadataOnlyFileYieldsNoParameters();
    void readReceiverParamsReadsTheShippedReceiversBlock();
    void readReceiverParamsAcceptsTheOlderParametersKeys();
    void receiverParamsFileRoundTripsScalarsAndWordMap();
    void receiverParamsFileFallsBackToTheDefaultWordMap();
    void receiverParamsFileIsNeverWrittenWithoutAWordMap();
};

#endif // TST_FRAMESETUP_H
