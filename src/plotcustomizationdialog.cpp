#include "plotcustomizationdialog.h"
#include "plotviewmodel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QTabWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QMap>

PlotCustomizationDialog::PlotCustomizationDialog(PlotViewModel* viewModel, QWidget* parent)
    : QDialog(parent)
    , m_viewModel(viewModel)
{
    setupUi();
    populateData();
}

void PlotCustomizationDialog::setupUi()
{
    setWindowTitle(tr("Customize Plot Series"));
    setMinimumSize(500, 400);

    auto* mainLayout = new QVBoxLayout(this);

    m_tabWidget = new QTabWidget(this);
    mainLayout->addWidget(m_tabWidget);

    // --- Frame Sync Lock Tab ---
    m_lockTab = new QWidget();
    auto* lockTabLayout = new QVBoxLayout(m_lockTab);
    
    auto* lockDesc = new QLabel(tr("Select the streams to display Frame Sync Lock data for.\n"
                                   "(This toggles both Lock % and Missed Frames plots)"), m_lockTab);
    lockTabLayout->addWidget(lockDesc);

    auto* lockBtnLayout = new QHBoxLayout();
    auto* lockSelectAllBtn = new QPushButton(tr("Select All"), m_lockTab);
    auto* lockSelectNoneBtn = new QPushButton(tr("Select None"), m_lockTab);
    lockBtnLayout->addWidget(lockSelectAllBtn);
    lockBtnLayout->addWidget(lockSelectNoneBtn);
    lockBtnLayout->addStretch();
    lockTabLayout->addLayout(lockBtnLayout);

    auto* lockScrollArea = new QScrollArea(m_lockTab);
    lockScrollArea->setWidgetResizable(true);
    // Budget for 8 visible rows (approx 200px tall depending on style)
    lockScrollArea->setMinimumHeight(200); 

    auto* lockScrollWidget = new QWidget();
    m_lockListLayout = new QVBoxLayout(lockScrollWidget);
    m_lockListLayout->setAlignment(Qt::AlignTop);
    lockScrollArea->setWidget(lockScrollWidget);
    lockTabLayout->addWidget(lockScrollArea);

    m_tabWidget->addTab(m_lockTab, tr("Frame Sync Lock Streams"));

    // --- Receiver SNR Tab ---
    m_snrTab = new QWidget();
    auto* snrTabLayout = new QVBoxLayout(m_snrTab);

    auto* snrTopLayout = new QHBoxLayout();
    snrTopLayout->addWidget(new QLabel(tr("Telemetry Stream Group:"), m_snrTab));
    m_snrStreamCombo = new QComboBox(m_snrTab);
    snrTopLayout->addWidget(m_snrStreamCombo, 1);
    snrTabLayout->addLayout(snrTopLayout);

    auto* snrBtnLayout = new QHBoxLayout();
    auto* snrSelectAllBtn = new QPushButton(tr("Select All"), m_snrTab);
    auto* snrSelectNoneBtn = new QPushButton(tr("Select None"), m_snrTab);
    snrBtnLayout->addWidget(snrSelectAllBtn);
    snrBtnLayout->addWidget(snrSelectNoneBtn);
    snrBtnLayout->addStretch();
    snrTabLayout->addLayout(snrBtnLayout);

    auto* snrScrollArea = new QScrollArea(m_snrTab);
    snrScrollArea->setWidgetResizable(true);
    m_snrGridContainer = new QWidget();
    m_snrGridLayout = new QGridLayout(m_snrGridContainer);
    m_snrGridLayout->setAlignment(Qt::AlignTop);
    snrScrollArea->setWidget(m_snrGridContainer);
    snrTabLayout->addWidget(snrScrollArea);

    m_tabWidget->addTab(m_snrTab, tr("Receiver SNR Streams"));

    // --- Dialog Buttons ---
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttonBox);

    // --- Connections ---
    connect(buttonBox, &QDialogButtonBox::accepted, this, &PlotCustomizationDialog::applyChanges);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_snrStreamCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PlotCustomizationDialog::onSnrStreamSelected);

    connect(lockSelectAllBtn, &QPushButton::clicked, this, &PlotCustomizationDialog::selectAllLock);
    connect(lockSelectNoneBtn, &QPushButton::clicked, this, &PlotCustomizationDialog::selectNoneLock);
    connect(snrSelectAllBtn, &QPushButton::clicked, this, &PlotCustomizationDialog::selectAllSnr);
    connect(snrSelectNoneBtn, &QPushButton::clicked, this, &PlotCustomizationDialog::selectNoneSnr);
}

void PlotCustomizationDialog::populateData()
{
    if (!m_viewModel) return;

    const auto& series = m_viewModel->allSeries();
    
    // Maps to collate series by streamOrder before creating UI elements
    QMap<int, QVector<int>> lockStreams; // streamOrder -> list of series indices
    QMap<int, QVector<int>> snrStreams;  // streamOrder -> list of series indices

    for (int i = 0; i < series.size(); ++i) {
        const auto& s = series.at(i);
        if (s.metricType == PlotSeriesData::MetricType::FrameSyncLock || 
            s.metricType == PlotSeriesData::MetricType::AccumulatedMissedFrames) {
            lockStreams[s.streamOrder].append(i);
        } else if (s.metricType == PlotSeriesData::MetricType::SNR) {
            snrStreams[s.streamOrder].append(i);
        }
    }

    // Populate Lock Tab
    for (auto it = lockStreams.begin(); it != lockStreams.end(); ++it) {
        int streamOrder = it.key();
        
        // Find if any series in this group is currently visible
        bool anyVisible = false;
        for (int idx : it.value()) {
            if (series.at(idx).visible) {
                anyVisible = true;
                break;
            }
        }

        auto* cb = new QCheckBox(QString("Stream %1").arg(streamOrder), m_lockTab);
        cb->setChecked(anyVisible);
        m_lockListLayout->addWidget(cb);
        
        m_lockCheckboxes.append(cb);
        m_lockCheckboxToSeriesIndices.insert(cb, it.value());
    }

    // Populate SNR Tab
    for (auto it = snrStreams.begin(); it != snrStreams.end(); ++it) {
        int streamOrder = it.key();
        m_snrStreamCombo->addItem(QString("Stream %1").arg(streamOrder), streamOrder);

        QVector<QPair<QCheckBox*, int>> checkboxesForStream;
        for (int idx : it.value()) {
            const auto& s = series.at(idx);
            auto* cb = new QCheckBox(QString("RCVR %1").arg(s.receiverIndex), m_snrGridContainer);
            cb->setChecked(s.visible);
            cb->hide(); // Hidden by default until stream is selected
            checkboxesForStream.append({cb, idx});
        }
        m_snrStreamGroups.insert(streamOrder, checkboxesForStream);
    }

    // Trigger initial selection for SNR tab
    if (m_snrStreamCombo->count() > 0) {
        onSnrStreamSelected(0);
    } else {
        m_snrStreamCombo->setEnabled(false);
    }
}

void PlotCustomizationDialog::clearSnrGrid()
{
    // Remove all items from grid layout but don't delete the widgets
    QLayoutItem* item;
    while ((item = m_snrGridLayout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            item->widget()->hide();
        }
        delete item; // Delete the layout item, not the widget
    }
}

void PlotCustomizationDialog::onSnrStreamSelected(int index)
{
    if (index < 0) return;
    clearSnrGrid();
    
    int streamOrder = m_snrStreamCombo->itemData(index).toInt();
    const auto& checkboxes = m_snrStreamGroups.value(streamOrder);

    // 8 columns wide for density
    const int cols = 8;
    int row = 0;
    int col = 0;

    for (const auto& pair : checkboxes) {
        pair.first->show();
        m_snrGridLayout->addWidget(pair.first, row, col);
        
        col++;
        if (col >= cols) {
            col = 0;
            row++;
        }
    }
}

void PlotCustomizationDialog::applyChanges()
{
    if (!m_viewModel) return;

    // Apply lock checkboxes
    for (auto* cb : m_lockCheckboxes) {
        bool visible = cb->isChecked();
        const auto& indices = m_lockCheckboxToSeriesIndices.value(cb);
        for (int idx : indices) {
            m_viewModel->setSeriesVisible(idx, visible);
        }
    }

    // Apply SNR checkboxes
    for (const auto& group : m_snrStreamGroups) {
        for (const auto& pair : group) {
            m_viewModel->setSeriesVisible(pair.second, pair.first->isChecked());
        }
    }

    accept();
}

void PlotCustomizationDialog::selectAllLock()
{
    for (auto* cb : m_lockCheckboxes) {
        cb->setChecked(true);
    }
}

void PlotCustomizationDialog::selectNoneLock()
{
    for (auto* cb : m_lockCheckboxes) {
        cb->setChecked(false);
    }
}

void PlotCustomizationDialog::selectAllSnr()
{
    if (m_snrStreamCombo->currentIndex() < 0) return;
    int streamOrder = m_snrStreamCombo->currentData().toInt();
    const auto& checkboxes = m_snrStreamGroups.value(streamOrder);
    for (const auto& pair : checkboxes) {
        pair.first->setChecked(true);
    }
}

void PlotCustomizationDialog::selectNoneSnr()
{
    if (m_snrStreamCombo->currentIndex() < 0) return;
    int streamOrder = m_snrStreamCombo->currentData().toInt();
    const auto& checkboxes = m_snrStreamGroups.value(streamOrder);
    for (const auto& pair : checkboxes) {
        pair.first->setChecked(false);
    }
}
