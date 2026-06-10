#include "tst_exportdialog.h"
#include "exportdialog.h"

#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QtTest>

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

    // Initial state (from constructor): CSV true, Image false
    QVERIFY(csvEdit->isEnabled() == true);
    QVERIFY(imgEdit->isEnabled() == false);

    // Toggle Image
    imgCheck->setChecked(true);
    QVERIFY(imgEdit->isEnabled() == true);

    // Toggle CSV
    csvCheck->setChecked(false);
    QVERIFY(csvEdit->isEnabled() == false);
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

    // Default state: CSV checked and populated -> valid
    QVERIFY(exportBtn->isEnabled() == true);

    // Clear CSV text
    csvEdit->setText("");
    QVERIFY(exportBtn->isEnabled() == false);

    // Check Image, still empty text -> invalid (CSV also checked+empty)
    imgCheck->setChecked(true);
    imgEdit->setText("");
    QVERIFY(exportBtn->isEnabled() == false);

    // Switch to image-only export: uncheck the empty CSV and give the image a
    // path. Every checked export now has a non-empty path -> valid.
    csvCheck->setChecked(false);
    imgEdit->setText("test.png");
    QVERIFY(exportBtn->isEnabled() == true);
}
