#include "processingprogressdialog.h"

#include <QCloseEvent>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include "constants.h"

ProcessingProgressDialog::ProcessingProgressDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Processing");
    setMinimumWidth(360);
    setWindowFlags((windowFlags() | Qt::CustomizeWindowHint) & ~Qt::WindowCloseButtonHint);

    m_status_label = new QLabel("Processing...");
    m_progress_bar = new QProgressBar;
    m_progress_bar->setMinimum(0);
    m_progress_bar->setMaximum(UIConstants::kProgressBarMax);

    m_cancel_button = new QPushButton("Cancel");
    connect(m_cancel_button, &QPushButton::clicked, this, [this]() {
        m_status_label->setText("Cancelling...");
        m_cancel_button->setEnabled(false);
        emit cancelRequested();
    });

    QHBoxLayout* button_layout = new QHBoxLayout;
    button_layout->addStretch();
    button_layout->addWidget(m_cancel_button);

    QVBoxLayout* main_layout = new QVBoxLayout(this);
    main_layout->addWidget(m_status_label);
    main_layout->addWidget(m_progress_bar);
    main_layout->addLayout(button_layout);
}

void ProcessingProgressDialog::reset()
{
    m_status_label->setText("Processing...");
    m_progress_bar->setValue(0);
    m_cancel_button->setEnabled(true);
}

void ProcessingProgressDialog::setProgress(int percent)
{
    m_progress_bar->setValue(percent);
}

void ProcessingProgressDialog::closeEvent(QCloseEvent* event)
{
    event->ignore();
}

void ProcessingProgressDialog::reject()
{
    // Escape is a no-op here; Cancel is the only way to stop processing.
}
