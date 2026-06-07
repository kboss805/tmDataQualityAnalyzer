/**
 * @file tst_mainview.cpp
 * @brief Smoke tests for MainView — construction, destruction, basic widget checks.
 */

#include "tst_mainview.h"

#include <QMenuBar>
#include <QToolBar>
#include <QtTest>

#include "mainview.h"

void TestMainView::constructsAndDestroysWithoutCrash()
{
    // Construction exercises the full widget hierarchy (toolbar, docks, tree, log, plot).
    // If any setup step throws or crashes, this test fails.
    MainView* view = new MainView();
    QVERIFY(view != nullptr);
    delete view;
}

void TestMainView::windowTitleIsNonEmpty()
{
    MainView view;
    QVERIFY2(!view.windowTitle().isEmpty(), "Window title must be set after construction");
}

void TestMainView::menuBarExists()
{
    MainView view;
    QVERIFY2(view.menuBar() != nullptr, "MainView must have a menu bar");
    QVERIFY2(!view.menuBar()->actions().isEmpty(), "Menu bar must have at least one menu");
}

void TestMainView::toolBarExists()
{
    MainView view;
    QList<QToolBar*> toolbars = view.findChildren<QToolBar*>();
    QVERIFY2(!toolbars.isEmpty(), "MainView must have at least one toolbar");
}
