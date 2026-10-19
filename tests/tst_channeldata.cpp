#include "tst_channeldata.h"

#include <QtTest>

#include "channeldata.h"

namespace {

// ChannelData's text is a QString today; the helpers below are the one place these tests
// read or write it, so a change of its string type touches them and nothing else.
QString typeOf(const ChannelData& cd) { return cd.channelType(); }
QString nameOf(const ChannelData& cd) { return cd.channelName(); }
void setType(ChannelData& cd, const QString& type) { cd.setChannelType(type); }
void setName(ChannelData& cd, const QString& name) { cd.setChannelName(name); }

} // namespace

void TestChannelData::constructorSetsChannelId()
{
    ChannelData cd(42);
    QCOMPARE(cd.channelID(), 42);
}

void TestChannelData::defaultChannelTypeIsEmpty()
{
    ChannelData cd(1);
    QVERIFY(typeOf(cd).isEmpty());
}

void TestChannelData::defaultChannelNameIsEmpty()
{
    ChannelData cd(1);
    QVERIFY(nameOf(cd).isEmpty());
}

void TestChannelData::defaultChannelCountIsZero()
{
    ChannelData cd(1);
    QCOMPARE(cd.channelCount(), 0);
}

void TestChannelData::setChannelTypeSetsType()
{
    ChannelData cd(1);
    setType(cd, "PCMIN");
    QCOMPARE(typeOf(cd), QString("PCMIN"));
}

void TestChannelData::setChannelNameSetsName()
{
    ChannelData cd(1);
    setName(cd, "TestChannel");
    QCOMPARE(nameOf(cd), QString("TestChannel"));
}

void TestChannelData::incrementChannelCountIncrementsOnce()
{
    ChannelData cd(1);
    cd.incrementChannelCount();
    QCOMPARE(cd.channelCount(), 1);
}

void TestChannelData::incrementChannelCountAccumulates()
{
    ChannelData cd(1);
    cd.incrementChannelCount();
    cd.incrementChannelCount();
    cd.incrementChannelCount();
    QCOMPARE(cd.channelCount(), 3);
}

void TestChannelData::nonAsciiTextRoundTrips()
{
    // TMATS names are free text; one with an accented letter and a Greek omega must come back
    // exactly as set, and setting again replaces rather than appends.
    const QString name = QStringLiteral("R") + QChar(0x00E9) + QStringLiteral("cepteur ") + QChar(0x03A9);
    const QString type = QStringLiteral("PCMIN-") + QChar(0x00E9);

    ChannelData cd(7);
    setName(cd, name);
    setType(cd, type);
    QCOMPARE(nameOf(cd), name);
    QCOMPARE(typeOf(cd), type);

    setName(cd, "plain");
    QCOMPARE(nameOf(cd), QString("plain"));
    setName(cd, QString());
    QVERIFY(nameOf(cd).isEmpty());
}

void TestChannelData::copiesKeepEveryField()
{
    // The reader stores these by value and the class is declared copyable and movable.
    ChannelData original(9);
    setName(original, "Name");
    setType(original, "TIMEIN");
    original.incrementChannelCount();
    original.incrementChannelCount();

    ChannelData copy(original);
    QCOMPARE(copy.channelID(), 9);
    QCOMPARE(nameOf(copy), QString("Name"));
    QCOMPARE(typeOf(copy), QString("TIMEIN"));
    QCOMPARE(copy.channelCount(), 2);

    // A copy is independent of the original.
    copy.incrementChannelCount();
    setName(copy, "Changed");
    QCOMPARE(original.channelCount(), 2);
    QCOMPARE(nameOf(original), QString("Name"));

    ChannelData moved(std::move(copy));
    QCOMPARE(moved.channelID(), 9);
    QCOMPARE(nameOf(moved), QString("Changed"));
    QCOMPARE(moved.channelCount(), 3);
}
