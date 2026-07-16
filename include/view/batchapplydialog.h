/**
 * @file batchapplydialog.h
 * @brief Modal dialog for Batch Apply: confirms which picked .ch10 files match a
 *        processing template and how to output the results.
 *
 * Matching is automatic (exact channel-ID set), so there is no per-file mapping
 * to adjust -- the dialog just shows each file's OK/rejected verdict, then lets
 * the user optionally also export a CSV+image per file (to a chosen folder) and,
 * for a saved template, reuse its per-series names/colors. Every processed file is
 * retained in memory regardless, browsable via the plot toolbar's file selector.
 */

#ifndef BATCHAPPLYDIALOG_H
#define BATCHAPPLYDIALOG_H

#include <QDialog>
#include <QList>
#include <QString>

class QCheckBox;
class QLineEdit;
class QPushButton;

class BatchApplyDialog : public QDialog
{
    Q_OBJECT

public:
    /// One picked file plus its up-front validation verdict.
    struct FileEntry
    {
        QString filepath;
        bool    ok = false;     ///< True if the file's channel-ID set matched the template.
        QString reason;         ///< Why it was rejected (empty when ok).
    };

    /// @param showReuseAppearance whether to offer the "reuse saved series
    /// names/colors" option (a saved template can carry appearance; a one-time
    /// Open Multiple Files config has none, so it hides the option).
    BatchApplyDialog(const QList<FileEntry>& files, const QString& defaultOutputDir,
                     bool showReuseAppearance, QWidget* parent = nullptr);

    bool    exportPerFile() const;   ///< True = also write a CSV+image per file to outputDir().
    bool    reuseAppearance() const; ///< True = reapply the template's saved series names/colors.
    QString outputDir() const;       ///< Directory for per-file CSV/image exports (when exportPerFile()).

private slots:
    void onExportToggled();
    void browseOutputDir();
    void validateInput();

private:
    void setUpLayout(const QList<FileEntry>& files);

    int m_ok_count = 0;

    QCheckBox*    m_export_checkbox;
    QCheckBox*    m_reuse_appearance_checkbox;
    QLineEdit*    m_output_dir_edit;
    QPushButton*  m_output_dir_btn;
    QPushButton*  m_apply_btn;
    QPushButton*  m_cancel_btn;
};

#endif // BATCHAPPLYDIALOG_H
