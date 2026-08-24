#ifndef TST_CHAPTER10READER_H
#define TST_CHAPTER10READER_H

#include <QObject>

class TestChapter10Reader : public QObject
{
    Q_OBJECT

private slots:
    // Runs before each test: skips the current test when the (gitignored, large)
    // .ch10 fixture is absent — e.g. on CI, where these files aren't checked out.
    // Per-test skip via init() (NOT a suite-level QSKIP in initTestCase(), which
    // poisons this multi-qExec harness and silently stops every later suite).
    void init();

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
};

#endif // TST_CHAPTER10READER_H
