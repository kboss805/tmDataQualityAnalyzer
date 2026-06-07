#ifndef TST_CHANNELDATA_H
#define TST_CHANNELDATA_H

#include <QObject>

class TestChannelData : public QObject
{
    Q_OBJECT

private slots:
    void constructorSetsChannelId();
    void defaultChannelTypeIsEmpty();
    void defaultChannelNameIsEmpty();
    void defaultChannelCountIsZero();
    void setChannelTypeSetsType();
    void setChannelNameSetsName();
    void incrementChannelCountIncrementsOnce();
    void incrementChannelCountAccumulates();
};

#endif // TST_CHANNELDATA_H
