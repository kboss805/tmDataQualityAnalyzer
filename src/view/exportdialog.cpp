#include "exportdialog.h"

#include <QBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>

ExportDialog::ExportDialog(const QString& defaultCsvPath, const QString& defaultImagePath,
                           const QString& defaultLogPath, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Export Data");
    setMinimumWidth(500);

    setUpLayout();

    m_csv_path_edit->setText(defaultCsvPath);
    m_image_path_edit->setText(defaultImagePath);
    m_log_path_edit->setText(defaultLogPath);

    m_image_checkbox->setChecked(true);
    m_csv_checkbox->setChecked(false);
    m_log_checkbox->setChecked(false);

    connect(m_csv_checkbox, &QCheckBox::toggled, this, &ExportDialog::validateInput);
    connect(m_image_checkbox, &QCheckBox::toggled, this, &ExportDialog::validateInput);
    connect(m_log_checkbox, &QCheckBox::toggled, this, &ExportDialog::validateInput);
    connect(m_csv_path_edit, &QLineEdit::textChanged, this, &ExportDialog::validateInput);
    connect(m_image_path_edit, &QLineEdit::textChanged, this, &ExportDialog::validateInput);
    connect(m_log_path_edit, &QLineEdit::textChanged, this, &ExportDialog::validateInput);

    validateInput();
}

void ExportDialog::setUpLayout()
{
    QVBoxLayout* main_layout = new QVBoxLayout(this);

    // Image Group (plot window) — listed first; the default export.
    QGroupBox* image_group = new QGroupBox("Export Plot Image");
    QVBoxLayout* image_layout = new QVBoxLayout(image_group);

    m_image_checkbox = new QCheckBox("Export Image");
    image_layout->addWidget(m_image_checkbox);

    QHBoxLayout* image_path_layout = new QHBoxLayout();
    m_image_path_edit = new QLineEdit();
    m_image_browse_btn = new QPushButton("Browse...");
    connect(m_image_browse_btn, &QPushButton::clicked, this, &ExportDialog::browseImagePath);
    image_path_layout->addWidget(m_image_path_edit);
    image_path_layout->addWidget(m_image_browse_btn);
    image_layout->addLayout(image_path_layout);

    main_layout->addWidget(image_group);

    // CSV Group
    QGroupBox* csv_group = new QGroupBox("Export Data (CSV)");
    QVBoxLayout* csv_layout = new QVBoxLayout(csv_group);

    m_csv_checkbox = new QCheckBox("Export Data");
    csv_layout->addWidget(m_csv_checkbox);

    QHBoxLayout* csv_path_layout = new QHBoxLayout();
    m_csv_path_edit = new QLineEdit();
    m_csv_browse_btn = new QPushButton("Browse...");
    connect(m_csv_browse_btn, &QPushButton::clicked, this, &ExportDialog::browseCsvPath);
    csv_path_layout->addWidget(m_csv_path_edit);
    csv_path_layout->addWidget(m_csv_browse_btn);
    csv_layout->addLayout(csv_path_layout);

    main_layout->addWidget(csv_group);

    // Log Group
    QGroupBox* log_group = new QGroupBox("Export Log (Text)");
    QVBoxLayout* log_layout = new QVBoxLayout(log_group);

    m_log_checkbox = new QCheckBox("Export Log");
    log_layout->addWidget(m_log_checkbox);

    QHBoxLayout* log_path_layout = new QHBoxLayout();
    m_log_path_edit = new QLineEdit();
    m_log_browse_btn = new QPushButton("Browse...");
    connect(m_log_browse_btn, &QPushButton::clicked, this, &ExportDialog::browseLogPath);
    log_path_layout->addWidget(m_log_path_edit);
    log_path_layout->addWidget(m_log_browse_btn);
    log_layout->addLayout(log_path_layout);

    main_layout->addWidget(log_group);

    // Buttons
    QHBoxLayout* button_layout = new QHBoxLayout();
    button_layout->addStretch();
    m_export_btn = new QPushButton("Export");
    m_cancel_btn = new QPushButton("Cancel");
    button_layout->addWidget(m_export_btn);
    button_layout->addWidget(m_cancel_btn);

    main_layout->addLayout(button_layout);

    connect(m_export_btn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_cancel_btn, &QPushButton::clicked, this, &QDialog::reject);
}

void ExportDialog::browseCsvPath()
{
    QString filename = QFileDialog::getSaveFileName(this, "Export CSV Data", m_csv_path_edit->text(), "CSV Files (*.csv);;All Files (*.*)");
    if (!filename.isEmpty())
    {
        if (QFileInfo(filename).suffix().isEmpty()) filename += ".csv";
        m_csv_path_edit->setText(filename);
    }
}

void ExportDialog::browseImagePath()
{
    QString filename = QFileDialog::getSaveFileName(this, "Export Plot Image", m_image_path_edit->text(), "PNG Files (*.png);;PDF Files (*.pdf);;SVG Files (*.svg);;All Files (*.*)");
    if (!filename.isEmpty())
    {
        if (QFileInfo(filename).suffix().isEmpty()) filename += ".png";
        m_image_path_edit->setText(filename);
    }
}

void ExportDialog::browseLogPath()
{
    QString filename = QFileDialog::getSaveFileName(this, "Export Log", m_log_path_edit->text(), "Text Files (*.txt);;All Files (*.*)");
    if (!filename.isEmpty())
    {
        if (QFileInfo(filename).suffix().isEmpty()) filename += ".txt";
        m_log_path_edit->setText(filename);
    }
}

void ExportDialog::validateInput()
{
    bool csv_ok = !m_csv_checkbox->isChecked() || !m_csv_path_edit->text().isEmpty();
    bool img_ok = !m_image_checkbox->isChecked() || !m_image_path_edit->text().isEmpty();
    bool log_ok = !m_log_checkbox->isChecked() || !m_log_path_edit->text().isEmpty();
    bool at_least_one = m_csv_checkbox->isChecked() || m_image_checkbox->isChecked()
                        || m_log_checkbox->isChecked();

    m_csv_path_edit->setEnabled(m_csv_checkbox->isChecked());
    m_csv_browse_btn->setEnabled(m_csv_checkbox->isChecked());

    m_image_path_edit->setEnabled(m_image_checkbox->isChecked());
    m_image_browse_btn->setEnabled(m_image_checkbox->isChecked());

    m_log_path_edit->setEnabled(m_log_checkbox->isChecked());
    m_log_browse_btn->setEnabled(m_log_checkbox->isChecked());

    m_export_btn->setEnabled(at_least_one && csv_ok && img_ok && log_ok);
}

bool ExportDialog::exportCsv() const { return m_csv_checkbox->isChecked(); }
bool ExportDialog::exportImage() const { return m_image_checkbox->isChecked(); }
bool ExportDialog::exportLog() const { return m_log_checkbox->isChecked(); }
QString ExportDialog::csvPath() const { return m_csv_path_edit->text(); }
QString ExportDialog::imagePath() const { return m_image_path_edit->text(); }
QString ExportDialog::logPath() const { return m_log_path_edit->text(); }
