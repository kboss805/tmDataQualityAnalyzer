#ifndef TST_MAINVIEWMODEL_HELPERS_H
#define TST_MAINVIEWMODEL_HELPERS_H

#include <QObject>

class TestMainViewModelHelpers : public QObject
{
    Q_OBJECT

private slots:
    void classifyLogMessageSeverity();
};

#endif // TST_MAINVIEWMODEL_HELPERS_H
