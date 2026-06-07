/**
 * @file tst_mainview.h
 * @brief Smoke tests for MainView — construction, destruction, log helpers.
 */

#ifndef TST_MAINVIEW_H
#define TST_MAINVIEW_H

#include <QObject>

class TestMainView : public QObject
{
    Q_OBJECT

private slots:
    void constructsAndDestroysWithoutCrash();
    void windowTitleIsNonEmpty();
    void menuBarExists();
    void toolBarExists();
};

#endif // TST_MAINVIEW_H
