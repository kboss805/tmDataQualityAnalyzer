#ifndef PROCESSINGPROGRESSDIALOG_H
#define PROCESSINGPROGRESSDIALOG_H

#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;

/**
 * @brief Modal dialog shown while background processing runs.
 *
 * Replaces the old always-visible progress bar (left dock) and Cancel toolbar
 * action: MainView shows this on processingChanged(true) and hides it again
 * once processing actually stops. The user can only dismiss it via the
 * Cancel button — the window close button is removed and Escape is blocked
 * so a stray dismissal doesn't leave the run silently going in the background.
 */
class ProcessingProgressDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProcessingProgressDialog(QWidget* parent = nullptr);

    /// Resets to 0% with the Cancel button re-enabled; call before show().
    void reset();

public slots:
    /// Updates the progress bar value (0..100).
    void setProgress(int percent);

signals:
    /// Emitted when the user clicks Cancel. The dialog only disables its own
    /// button and updates its label — the owner is responsible for hiding the
    /// dialog once processing actually stops.
    void cancelRequested();

protected:
    /// Blocks Alt+F4/system-menu close; Cancel is the only way out.
    void closeEvent(QCloseEvent* event) override;
    /// Blocks Escape from dismissing the dialog while processing is active.
    void reject() override;

private:
    QLabel* m_status_label;
    QProgressBar* m_progress_bar;
    QPushButton* m_cancel_button;
};

#endif // PROCESSINGPROGRESSDIALOG_H
