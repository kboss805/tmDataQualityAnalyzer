#ifndef TST_CHAPTER10READER_H
#define TST_CHAPTER10READER_H

#include <QObject>

class TestChapter10Reader : public QObject
{
    Q_OBJECT

private slots:
    // Skips the whole suite when the (gitignored, large) .ch10 fixture is absent —
    // e.g. on CI, where these files aren't checked out. Runs normally locally.
    void initTestCase();

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
    void getTimeChannelIndexReturnsValidIndex();
    void getTimeChannelIndexReturnsMinusOneForUnknown();
    void getPcmChannelIndexReturnsValidIndex();
    void getPcmChannelIndexReturnsMinusOneForUnknown();
};

#endif // TST_CHAPTER10READER_H
