#ifndef EXPORTDIALOG_H
#define EXPORTDIALOG_H

#include <QCheckBox>
#include <QDialog>
#include <QLineEdit>
#include <QPushButton>

class ExportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ExportDialog(const QString& defaultCsvPath, const QString& defaultImagePath,
                          const QString& defaultLogPath = QString(), QWidget *parent = nullptr);

    bool exportCsv() const;
    bool exportImage() const;
    bool exportLog() const;
    QString csvPath() const;
    QString imagePath() const;
    QString logPath() const;

private slots:
    void browseCsvPath();
    void browseImagePath();
    void browseLogPath();
    void validateInput();

private:
    void setUpLayout();

    QCheckBox* m_csv_checkbox;
    QLineEdit* m_csv_path_edit;
    QPushButton* m_csv_browse_btn;

    QCheckBox* m_image_checkbox;
    QLineEdit* m_image_path_edit;
    QPushButton* m_image_browse_btn;

    QCheckBox* m_log_checkbox;
    QLineEdit* m_log_path_edit;
    QPushButton* m_log_browse_btn;

    QPushButton* m_export_btn;
    QPushButton* m_cancel_btn;
};

#endif // EXPORTDIALOG_H
