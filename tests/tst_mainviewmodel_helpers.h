#ifndef TST_MAINVIEWMODEL_HELPERS_H
#define TST_MAINVIEWMODEL_HELPERS_H

#include <QObject>

class TestMainViewModelHelpers : public QObject
{
    Q_OBJECT

private slots:
    void channelPrefixKnownIndices();
    void channelPrefixUnknownIndices();
    void parameterNameKnownChannels();
    void parameterNameUnknownChannels();
    void channelPrefixBoundaryIndex();
    void channelPrefixLargeIndex();
};

#endif // TST_MAINVIEWMODEL_HELPERS_H
