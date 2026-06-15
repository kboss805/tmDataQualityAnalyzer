#ifndef TST_EXPORTDIALOG_H
#define TST_EXPORTDIALOG_H

#include <QObject>

class TestExportDialog : public QObject
{
    Q_OBJECT

private slots:
    void testCheckboxTogglesFields();
    void testExportButtonValidation();
    void testLogRowDefaultsAndAccessors();
    void testLogOnlyExportValidation();
};

#endif // TST_EXPORTDIALOG_H
