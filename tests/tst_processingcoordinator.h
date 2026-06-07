#ifndef TST_PROCESSINGCOORDINATOR_H
#define TST_PROCESSINGCOORDINATOR_H

#include <QObject>

class TestProcessingCoordinator : public QObject
{
    Q_OBJECT

private slots:
    void constructorDefaults();
    void resetClearsState();
    void cancelProcessingNoRunNoOp();
    void startProcessingEmptyReturnsFalse();
    void startProcessingEmitsProcessingState();
};

#endif // TST_PROCESSINGCOORDINATOR_H
