#ifndef TST_CHAPTER10READER_H
#define TST_CHAPTER10READER_H

#include <QObject>

class TestChapter10Reader : public QObject
{
    Q_OBJECT

private slots:
    void loadChannelsReturnsTrueForValidFile();
    void loadChannelsPopulatesTimeChannels();
    void loadChannelsPopulatesPcmChannels();
    void comboBoxListsContainChannelIdAndName();
    void currentTimeChannelSetAfterLoad();
    void currentPcmChannelSetAfterLoad();
    void clearSettingsResetsChannels();
    void timeChannelChangedUpdatesSelection();
    void pcmChannelChangedUpdatesSelection();
    void loadChannelsReturnsFalseForInvalidFile();
    void getFirstPcmChannelIdReturnsValidId();
    void timeAccessorsReturnNonZeroAfterLoad();
    void getTimeChannelIndexReturnsValidIndex();
    void getTimeChannelIndexReturnsMinusOneForUnknown();
    void getPcmChannelIndexReturnsValidIndex();
    void getPcmChannelIndexReturnsMinusOneForUnknown();

    // v3.2 additions
    void dhmsToUInt64ComputesCorrectOffset();
};

#endif // TST_CHAPTER10READER_H
