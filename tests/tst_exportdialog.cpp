#include "tst_exportdialog.h"

#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QtTest>

#include "exportdialog.h"

void TestExportDialog::testCheckboxTogglesFields()
{
    ExportDialog dlg("default.csv", "default.png");

    QList<QCheckBox*> checkboxes = dlg.findChildren<QCheckBox*>();
    QCheckBox* csvCheck = nullptr;
    QCheckBox* imgCheck = nullptr;
    for (auto* cb : checkboxes) {
        if (cb->text() == "Export Data") csvCheck = cb;
        if (cb->text() == "Export Image") imgCheck = cb;
    }
    QVERIFY(csvCheck != nullptr);
    QVERIFY(imgCheck != nullptr);

    QList<QLineEdit*> lineEdits = dlg.findChildren<QLineEdit*>();
    QVERIFY(lineEdits.size() >= 2);
    QLineEdit* csvEdit = nullptr;
    QLineEdit* imgEdit = nullptr;
    for (auto* le : lineEdits) {
        if (le->text() == "default.csv") csvEdit = le;
        if (le->text() == "default.png") imgEdit = le;
    }
    QVERIFY(csvEdit != nullptr);
    QVERIFY(imgEdit != nullptr);

    // Initial state (from constructor): Image true, CSV false
    QVERIFY(imgEdit->isEnabled() == true);
    QVERIFY(csvEdit->isEnabled() == false);

    // Toggle CSV on
    csvCheck->setChecked(true);
    QVERIFY(csvEdit->isEnabled() == true);

    // Toggle Image off
    imgCheck->setChecked(false);
    QVERIFY(imgEdit->isEnabled() == false);
}

void TestExportDialog::testExportButtonValidation()
{
    ExportDialog dlg("default.csv", "default.png");

    QList<QPushButton*> buttons = dlg.findChildren<QPushButton*>();
    QPushButton* exportBtn = nullptr;
    for (auto* btn : buttons) {
        if (btn->text() == "Export") exportBtn = btn;
    }
    QVERIFY(exportBtn != nullptr);

    QList<QCheckBox*> checkboxes = dlg.findChildren<QCheckBox*>();
    QCheckBox* csvCheck = nullptr;
    QCheckBox* imgCheck = nullptr;
    for (auto* cb : checkboxes) {
        if (cb->text() == "Export Data") csvCheck = cb;
        if (cb->text() == "Export Image") imgCheck = cb;
    }

    QList<QLineEdit*> lineEdits = dlg.findChildren<QLineEdit*>();
    QLineEdit* csvEdit = nullptr;
    QLineEdit* imgEdit = nullptr;
    for (auto* le : lineEdits) {
        if (le->text() == "default.csv") csvEdit = le;
        if (le->text() == "default.png") imgEdit = le;
    }

    // Default state: Image checked and populated -> valid
    QVERIFY(exportBtn->isEnabled() == true);

    // Clear Image text
    imgEdit->setText("");
    QVERIFY(exportBtn->isEnabled() == false);

    // Check CSV, still empty text -> invalid (Image also checked+empty)
    csvCheck->setChecked(true);
    csvEdit->setText("");
    QVERIFY(exportBtn->isEnabled() == false);

    // Switch to CSV-only export: uncheck the empty Image and give the CSV a
    // path. Every checked export now has a non-empty path -> valid.
    imgCheck->setChecked(false);
    csvEdit->setText("test.csv");
    QVERIFY(exportBtn->isEnabled() == true);
}

void TestExportDialog::testLogRowDefaultsAndAccessors()
{
    ExportDialog dlg("default.csv", "default.png", "default_log.txt");

    QCheckBox* logCheck = nullptr;
    for (auto* cb : dlg.findChildren<QCheckBox*>()) {
        if (cb->text() == "Export Log") logCheck = cb;
    }
    QVERIFY(logCheck != nullptr);

    QLineEdit* logEdit = nullptr;
    for (auto* le : dlg.findChildren<QLineEdit*>()) {
        if (le->text() == "default_log.txt") logEdit = le;
    }
    QVERIFY(logEdit != nullptr);

    // Default: log export off, so its accessors and field reflect that.
    QVERIFY(logCheck->isChecked() == false);
    QVERIFY(dlg.exportLog() == false);
    QVERIFY(logEdit->isEnabled() == false);
    QCOMPARE(dlg.logPath(), QString("default_log.txt"));

    // Enabling the checkbox enables the path field and flips the accessor.
    logCheck->setChecked(true);
    QVERIFY(dlg.exportLog() == true);
    QVERIFY(logEdit->isEnabled() == true);
}

void TestExportDialog::testLogOnlyExportValidation()
{
    ExportDialog dlg("default.csv", "default.png", "default_log.txt");

    QPushButton* exportBtn = nullptr;
    for (auto* btn : dlg.findChildren<QPushButton*>()) {
        if (btn->text() == "Export") exportBtn = btn;
    }
    QVERIFY(exportBtn != nullptr);

    QCheckBox* imgCheck = nullptr;
    QCheckBox* logCheck = nullptr;
    for (auto* cb : dlg.findChildren<QCheckBox*>()) {
        if (cb->text() == "Export Image") imgCheck = cb;
        if (cb->text() == "Export Log") logCheck = cb;
    }
    QLineEdit* logEdit = nullptr;
    for (auto* le : dlg.findChildren<QLineEdit*>()) {
        if (le->text() == "default_log.txt") logEdit = le;
    }

    // Log-only export with a populated path is valid.
    imgCheck->setChecked(false);
    logCheck->setChecked(true);
    QVERIFY(exportBtn->isEnabled() == true);

    // Clearing the log path (the only checked export) invalidates the dialog.
    logEdit->setText("");
    QVERIFY(exportBtn->isEnabled() == false);
}
