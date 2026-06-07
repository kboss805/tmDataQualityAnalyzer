#ifndef EXPORTDIALOG_H
#define EXPORTDIALOG_H

#include <QDialog>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>

class ExportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ExportDialog(const QString& defaultCsvPath, const QString& defaultImagePath, QWidget *parent = nullptr);

    bool exportCsv() const;
    bool exportImage() const;
    QString csvPath() const;
    QString imagePath() const;

private slots:
    void browseCsvPath();
    void browseImagePath();
    void validateInput();

private:
    void setUpLayout();

    QCheckBox* m_csv_checkbox;
    QLineEdit* m_csv_path_edit;
    QPushButton* m_csv_browse_btn;

    QCheckBox* m_image_checkbox;
    QLineEdit* m_image_path_edit;
    QPushButton* m_image_browse_btn;

    QPushButton* m_export_btn;
    QPushButton* m_cancel_btn;
};

#endif // EXPORTDIALOG_H
