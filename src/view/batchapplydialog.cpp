/**
 * @file batchapplydialog.cpp
 * @brief Implementation of the Batch Apply confirmation dialog.
 */

#include "batchapplydialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

BatchApplyDialog::BatchApplyDialog(const QList<FileEntry>& files, const QString& defaultOutputDir,
                                   bool showReuseAppearance, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Batch Process Files"));
    setUpLayout(files);
    m_reuse_appearance_checkbox->setVisible(showReuseAppearance);
    if (!showReuseAppearance)
        m_reuse_appearance_checkbox->setChecked(false);
    m_output_dir_edit->setText(defaultOutputDir);
    onModeChanged();
    validateInput();
}

void BatchApplyDialog::setUpLayout(const QList<FileEntry>& files)
{
    auto* layout = new QVBoxLayout(this);

    m_ok_count = 0;
    for (const FileEntry& f : files)
    {
        if (f.ok)
            ++m_ok_count;
    }

    auto* summary = new QLabel(
        tr("%1 of %2 selected file(s) match the template and will be processed.")
            .arg(m_ok_count).arg(files.size()), this);
    summary->setWordWrap(true);
    layout->addWidget(summary);

    // The file list: OK rows enabled, rejected rows disabled with the reason.
    auto* list = new QListWidget(this);
    for (const FileEntry& f : files)
    {
        const QString name = QFileInfo(f.filepath).fileName();
        auto* item = new QListWidgetItem(list);
        if (f.ok)
        {
            item->setText(QStringLiteral("✓  ") + name);
        }
        else
        {
            item->setText(QStringLiteral("✗  ") + name + tr("  —  %1").arg(f.reason));
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        }
        item->setToolTip(f.filepath);
    }
    layout->addWidget(list);

    // Output mode: one merged plot, or a separate CSV+image per file.
    auto* mode_group = new QGroupBox(tr("Output"), this);
    auto* mode_layout = new QVBoxLayout(mode_group);
    m_merged_radio = new QRadioButton(tr("One merged plot (all files on a shared time axis)"), mode_group);
    m_separate_radio = new QRadioButton(tr("Separate CSV + image per file"), mode_group);
    m_merged_radio->setChecked(true);
    mode_layout->addWidget(m_merged_radio);
    mode_layout->addWidget(m_separate_radio);

    auto* dir_row = new QHBoxLayout();
    dir_row->addWidget(new QLabel(tr("Output folder:"), mode_group));
    m_output_dir_edit = new QLineEdit(mode_group);
    m_output_dir_btn = new QPushButton(tr("Browse..."), mode_group);
    dir_row->addWidget(m_output_dir_edit, 1);
    dir_row->addWidget(m_output_dir_btn);
    mode_layout->addLayout(dir_row);

    layout->addWidget(mode_group);

    m_reuse_appearance_checkbox =
        new QCheckBox(tr("Reuse the template's saved series names and colors"), this);
    m_reuse_appearance_checkbox->setChecked(true);
    layout->addWidget(m_reuse_appearance_checkbox);

    // WinUI convention: primary action left of Cancel.
    auto* buttons = new QDialogButtonBox(this);
    m_apply_btn = buttons->addButton(tr("Process"), QDialogButtonBox::AcceptRole);
    m_cancel_btn = buttons->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(buttons);

    connect(m_merged_radio, &QRadioButton::toggled, this, &BatchApplyDialog::onModeChanged);
    connect(m_output_dir_edit, &QLineEdit::textChanged, this, &BatchApplyDialog::validateInput);
    connect(m_output_dir_btn, &QPushButton::clicked, this, &BatchApplyDialog::browseOutputDir);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void BatchApplyDialog::onModeChanged()
{
    // The output folder only matters for separate-per-file exports.
    const bool separate = m_separate_radio->isChecked();
    m_output_dir_edit->setEnabled(separate);
    m_output_dir_btn->setEnabled(separate);
    validateInput();
}

void BatchApplyDialog::browseOutputDir()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Select Output Folder"),
        m_output_dir_edit->text());
    if (!dir.isEmpty())
        m_output_dir_edit->setText(dir);
}

void BatchApplyDialog::validateInput()
{
    // Need at least one matching file, and (separate mode) an output folder.
    bool valid = m_ok_count > 0;
    if (m_separate_radio->isChecked() && m_output_dir_edit->text().trimmed().isEmpty())
        valid = false;
    m_apply_btn->setEnabled(valid);
}

bool BatchApplyDialog::mergedMode() const
{
    return m_merged_radio->isChecked();
}

bool BatchApplyDialog::reuseAppearance() const
{
    return m_reuse_appearance_checkbox->isChecked();
}

QString BatchApplyDialog::outputDir() const
{
    return m_output_dir_edit->text().trimmed();
}
