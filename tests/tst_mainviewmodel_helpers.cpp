#include "tst_mainviewmodel_helpers.h"
#include <QtTest>

#include "mainviewmodel.h"

void TestMainViewModelHelpers::classifyLogMessageSeverity()
{
    using LL = MainViewModel::LogLevel;
    // ERROR / WARNING are matched anywhere; the success cues anchor at the start.
    QCOMPARE(static_cast<int>(MainViewModel::classifyLogMessage("File read ERROR; aborting.")),
             static_cast<int>(LL::Error));
    QCOMPARE(static_cast<int>(MainViewModel::classifyLogMessage("WARNING: 2 time gap(s) detected.")),
             static_cast<int>(LL::Warning));
    QCOMPARE(static_cast<int>(MainViewModel::classifyLogMessage("Pre-scan result: 3 PCM channels.")),
             static_cast<int>(LL::Success));
    QCOMPARE(static_cast<int>(MainViewModel::classifyLogMessage("Processing complete.")),
             static_cast<int>(LL::Success));
    QCOMPARE(static_cast<int>(MainViewModel::classifyLogMessage("Opening: test.ch10")),
             static_cast<int>(LL::Info));
    // Error outranks the other cues when several are present.
    QCOMPARE(static_cast<int>(MainViewModel::classifyLogMessage("ERROR with a WARNING too")),
             static_cast<int>(LL::Error));
}
