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
#include <QVBoxLayout>

#include "constants.h"

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
    onExportToggled();
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
    list->setMinimumHeight(UIConstants::kFileListMinHeight);
    for (const FileEntry& f : files)
    {
        const QString name = QFileInfo(f.filepath).fileName();
        auto* item = new QListWidgetItem(list);

        // Colour only the glyph, matching the Configure Streams Ready column - a
        // QListWidgetItem renders plain text, so the row carries a QLabel instead.
        const QString glyph = f.ok ? QStringLiteral("✓") : QStringLiteral("✗");
        const QString color = f.ok ? UIConstants::kStatusOkColor
                                   : UIConstants::kStatusFailColor;
        const QString trailer = f.ok ? QString() : tr("  —  %1").arg(f.reason);
        auto* row = new QLabel(QString("<span style='color: %1;'>%2</span>  %3%4")
                                   .arg(color, glyph, name.toHtmlEscaped(), trailer.toHtmlEscaped()),
                               list);
        row->setTextFormat(Qt::RichText);

        // Rejected rows are made unselectable rather than DISABLED. Qt greys a
        // disabled item's contents regardless of any colour set on it, so disabling
        // would silently defeat the red glyph this exists to show. The row is inert
        // either way - nothing reads this list's selection.
        if (!f.ok)
        {
            item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        }
        item->setSizeHint(row->sizeHint());
        list->setItemWidget(item, row);
        item->setToolTip(f.filepath);
    }
    layout->addWidget(list);

    // Every processed file is kept in memory and browsable via the plot toolbar's
    // file selector. Optionally also write a CSV+image per file to disk.
    auto* out_group = new QGroupBox(tr("Output"), this);
    auto* out_layout = new QVBoxLayout(out_group);
    m_export_checkbox = new QCheckBox(
        tr("Also export a CSV + plot images (Frame Sync Lock and Missed Frames) per file"), out_group);
    out_layout->addWidget(m_export_checkbox);

    auto* dir_row = new QHBoxLayout();
    dir_row->addWidget(new QLabel(tr("Output folder:"), out_group));
    m_output_dir_edit = new QLineEdit(out_group);
    m_output_dir_btn = new QPushButton(tr("Browse..."), out_group);
    dir_row->addWidget(m_output_dir_edit, 1);
    dir_row->addWidget(m_output_dir_btn);
    out_layout->addLayout(dir_row);

    layout->addWidget(out_group);

    m_reuse_appearance_checkbox =
        new QCheckBox(tr("Reuse the template's saved series names and colors"), this);
    m_reuse_appearance_checkbox->setChecked(true);
    layout->addWidget(m_reuse_appearance_checkbox);

    // WinUI convention: primary action left of Cancel.
    auto* buttons = new QDialogButtonBox(this);
    m_apply_btn = buttons->addButton(tr("Process"), QDialogButtonBox::AcceptRole);
    m_cancel_btn = buttons->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(buttons);

    connect(m_export_checkbox, &QCheckBox::toggled, this, &BatchApplyDialog::onExportToggled);
    connect(m_output_dir_edit, &QLineEdit::textChanged, this, &BatchApplyDialog::validateInput);
    connect(m_output_dir_btn, &QPushButton::clicked, this, &BatchApplyDialog::browseOutputDir);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void BatchApplyDialog::onExportToggled()
{
    // The output folder only matters when per-file export is on.
    const bool exporting = m_export_checkbox->isChecked();
    m_output_dir_edit->setEnabled(exporting);
    m_output_dir_btn->setEnabled(exporting);
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
    // Need at least one matching file, and (per-file export on) an output folder.
    bool valid = m_ok_count > 0;
    if (m_export_checkbox->isChecked() && m_output_dir_edit->text().trimmed().isEmpty())
        valid = false;
    m_apply_btn->setEnabled(valid);
}

bool BatchApplyDialog::exportPerFile() const
{
    return m_export_checkbox->isChecked();
}

bool BatchApplyDialog::reuseAppearance() const
{
    return m_reuse_appearance_checkbox->isChecked();
}

QString BatchApplyDialog::outputDir() const
{
    return m_output_dir_edit->text().trimmed();
}
