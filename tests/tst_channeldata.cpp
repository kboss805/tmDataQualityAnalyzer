#include "tst_channeldata.h"

#include <QtTest>

#include "channeldata.h"

void TestChannelData::constructorSetsChannelId()
{
    ChannelData cd(42);
    QCOMPARE(cd.channelID(), 42);
}

void TestChannelData::defaultChannelTypeIsEmpty()
{
    ChannelData cd(1);
    QVERIFY(cd.channelType().isEmpty());
}

void TestChannelData::defaultChannelNameIsEmpty()
{
    ChannelData cd(1);
    QVERIFY(cd.channelName().isEmpty());
}

void TestChannelData::defaultChannelCountIsZero()
{
    ChannelData cd(1);
    QCOMPARE(cd.channelCount(), 0);
}

void TestChannelData::setChannelTypeSetsType()
{
    ChannelData cd(1);
    cd.setChannelType("PCMIN");
    QCOMPARE(cd.channelType(), QString("PCMIN"));
}

void TestChannelData::setChannelNameSetsName()
{
    ChannelData cd(1);
    cd.setChannelName("TestChannel");
    QCOMPARE(cd.channelName(), QString("TestChannel"));
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
