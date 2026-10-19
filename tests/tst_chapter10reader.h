#ifndef TST_CHAPTER10READER_H
#define TST_CHAPTER10READER_H

#include <QObject>

class TestChapter10Reader : public QObject
{
    Q_OBJECT

private slots:
    // Runs before each test: skips the current test when the .ch10 fixtures are absent
    // (a worktree without the tests/data junction). The fixtures themselves are committed.
    // Per-test skip via init() (NOT a suite-level QSKIP in initTestCase(), which
    // poisons this multi-qExec harness and silently stops every later suite).
    void init();

    void loadChannelsReturnsTrueForValidFile();
    void loadChannelsPopulatesTimeChannels();
    void loadChannelsPopulatesPcmChannels();
    void comboBoxListsContainChannelIdAndName();

    // Exact channel ids, names and TMATS data rates for each committed recording.
    void eachFixtureListsItsChannelsExactly();
    void eachFixtureListsItsChannelsExactly_data();
    void aChannelTmatsDoesNotDescribeHasNoDataRate();

    void currentTimeChannelSetAfterLoad();
    void currentPcmChannelSetAfterLoad();
    void clearSettingsResetsChannels();
    void timeChannelChangedUpdatesSelection();
    void pcmChannelChangedUpdatesSelection();
    void comboBoxIndexSelectsTheChannelAtThatPosition();

    void loadChannelsReturnsFalseForInvalidFile();
    // Each way a file can fail to load has its own message and leaves no channels behind.
    void aFailedLoadReportsItsReasonAndLeavesNoChannels();
    void aFailedLoadReportsItsReasonAndLeavesNoChannels_data();
    // TMATS text decides which channels are listed and what they are called.
    void tmatsTextDecidesWhichChannelsAreListedAndWhatTheyAreCalled();
    void tmatsTextDecidesWhichChannelsAreListedAndWhatTheyAreCalled_data();
    void aFileThatFailsToOpenLeavesThePreviousChannelsAlone();
    void getFirstPcmChannelIdReturnsValidId();
};

#endif // TST_CHAPTER10READER_H
