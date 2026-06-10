/**
 * @file timeextractionwidget.cpp
 * @brief Implementation of TimeExtractionWidget — time range controls.
 */

#include "timeextractionwidget.h"

#include <QChar>
#include <QString>

#include "constants.h"

TimeExtractionWidget::TimeExtractionWidget(QWidget* parent)
    : QWidget(parent)
    , m_time_all(new QCheckBox("Extract All"))
    , m_start_time(new QLineEdit)
    , m_stop_time(new QLineEdit)
{
    m_start_time->setInputMask("000:00:00:00;_");
    m_start_time->setPlaceholderText("DDD:HH:MM:SS");
    m_start_time->setMaximumWidth(UIConstants::kTimeInputMaxWidth);

    m_stop_time->setInputMask("000:00:00:00;_");
    m_stop_time->setPlaceholderText("DDD:HH:MM:SS");
    m_stop_time->setMaximumWidth(UIConstants::kTimeInputMaxWidth);

    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(0, 4, 0, 4);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(4);

    // Checkbox in col 0, row 0 only — naturally aligns with the Start row
    grid->addWidget(m_time_all,          0, 0, Qt::AlignVCenter);
    grid->addWidget(m_start_time,        0, 1);
    grid->addWidget(new QLabel("Start"), 0, 2);
    grid->addWidget(m_stop_time,         1, 1);
    grid->addWidget(new QLabel("Stop"),  1, 2);
    grid->setColumnStretch(3, 1);
    setLayout(grid);

    connect(m_time_all, &QAbstractButton::toggled, this, [this](bool checked) {
        m_start_time->setEnabled(!checked);
        m_stop_time->setEnabled(!checked);
        emit extractAllTimeChanged(checked);
    });

    connect(m_start_time, &QLineEdit::editingFinished,
            this, &TimeExtractionWidget::startTimeEditingFinished);
    connect(m_stop_time, &QLineEdit::editingFinished,
            this, &TimeExtractionWidget::stopTimeEditingFinished);
}

bool TimeExtractionWidget::extractAllTime() const
{
    return m_time_all->isChecked();
}

void TimeExtractionWidget::setExtractAllTime(bool value)
{
    QSignalBlocker blocker(m_time_all);
    m_time_all->setChecked(value);
    m_start_time->setEnabled(!value);
    m_stop_time->setEnabled(!value);
}

void TimeExtractionWidget::setAllEnabled(bool enabled)
{
    m_time_all->setEnabled(enabled);
    if (enabled)
    {
        if (!m_time_all->isChecked())
        {
            m_start_time->setEnabled(true);
            m_stop_time->setEnabled(true);
        }
    }
    else
    {
        m_start_time->setEnabled(false);
        m_stop_time->setEnabled(false);
    }
}

void TimeExtractionWidget::fillTimes(const TimeFields& start, const TimeFields& stop)
{
    m_start_time->setText(
        QString("%1:%2:%3:%4")
            .arg(start.ddd, 3, UIConstants::kDecimalBase, QChar('0'))
            .arg(start.hh, 2, UIConstants::kDecimalBase, QChar('0'))
            .arg(start.mm, 2, UIConstants::kDecimalBase, QChar('0'))
            .arg(start.ss, 2, UIConstants::kDecimalBase, QChar('0')));

    m_stop_time->setText(
        QString("%1:%2:%3:%4")
            .arg(stop.ddd, 3, UIConstants::kDecimalBase, QChar('0'))
            .arg(stop.hh, 2, UIConstants::kDecimalBase, QChar('0'))
            .arg(stop.mm, 2, UIConstants::kDecimalBase, QChar('0'))
            .arg(stop.ss, 2, UIConstants::kDecimalBase, QChar('0')));
}

void TimeExtractionWidget::clearTimes()
{
    m_start_time->clear();
    m_stop_time->clear();
}

QString TimeExtractionWidget::startTimeText() const
{
    return m_start_time->text();
}

QString TimeExtractionWidget::stopTimeText() const
{
    return m_stop_time->text();
}
