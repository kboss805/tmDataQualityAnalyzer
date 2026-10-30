#ifndef TST_CONSTANTS_H
#define TST_CONSTANTS_H

#include <QObject>

/// plotconstants.h, pinned by value. constants.h is Qt-free and pinned by the native suite
/// (native/tmdq_core_tests/test_constants.cpp, plan step 2.4).
class TestConstants : public QObject
{
    Q_OBJECT

private slots:
    void plotConstants();
};

#endif // TST_CONSTANTS_H
