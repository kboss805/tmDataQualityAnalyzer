#include "plotcustomizationdialog.h"
#include "plotviewmodel.h"
#include "constants.h"

#include <algorithm>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QComboBox>
#include <QCheckBox>
#include <QTreeWidget>
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
    m_snrExpandBtn = new QPushButton(tr("Expand All"), m_snrTab);
    auto* snrSelectAllBtn = new QPushButton(tr("Select All"), m_snrTab);
    auto* snrSelectNoneBtn = new QPushButton(tr("Select None"), m_snrTab);
    snrBtnLayout->addWidget(m_snrExpandBtn);
    snrBtnLayout->addWidget(snrSelectAllBtn);
    snrBtnLayout->addWidget(snrSelectNoneBtn);
    snrBtnLayout->addStretch();
    snrTabLayout->addLayout(snrBtnLayout);

    auto* snrScrollArea = new QScrollArea(m_snrTab);
    snrScrollArea->setWidgetResizable(true);

    auto* snrTreeContainer = new QWidget();
    m_snrTreeLayout = new QVBoxLayout(snrTreeContainer);
    m_snrTreeLayout->setContentsMargins(0, 0, 0, 0);
    m_snrTreeLayout->setAlignment(Qt::AlignTop);
    snrScrollArea->setWidget(snrTreeContainer);
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
    connect(m_snrExpandBtn, &QPushButton::clicked, this, &PlotCustomizationDialog::toggleExpandCollapseSnr);
    connect(snrSelectAllBtn, &QPushButton::clicked, this, &PlotCustomizationDialog::selectAllSnr);
    connect(snrSelectNoneBtn, &QPushButton::clicked, this, &PlotCustomizationDialog::selectNoneSnr);
}

void PlotCustomizationDialog::populateData()
{
    if (!m_viewModel) return;

    const auto& series = m_viewModel->allSeries();

    // streamOrder -> list of series indices (Lock tab)
    QMap<int, QVector<int>> lockStreams;
    // streamOrder -> receiverIndex -> list of series indices (SNR tab)
    QMap<int, QMap<int, QVector<int>>> snrStreams;

    for (int i = 0; i < series.size(); ++i) {
        const auto& s = series.at(i);
        if (s.metricType == PlotSeriesData::MetricType::FrameSyncLock ||
            s.metricType == PlotSeriesData::MetricType::AccumulatedMissedFrames) {
            lockStreams[s.streamOrder].append(i);
        } else if (s.metricType == PlotSeriesData::MetricType::SNR) {
            snrStreams[s.streamOrder][s.receiverIndex].append(i);
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

    // Populate SNR Tab: one or two trees per stream (columns), each with RCVR groups
    // containing L/R/C channel checkboxes.
    for (auto streamIt = snrStreams.begin(); streamIt != snrStreams.end(); ++streamIt) {
        int streamOrder = streamIt.key();
        const auto& receivers = streamIt.value();

        // QMap keys are already sorted ascending by receiver index.
        const QVector<int> receiverIndices = receivers.keys();
        const int splitPoint = (receiverIndices.size() + 1) / 2;
        const QVector<int> leftIndices = receiverIndices.mid(0, splitPoint);
        const QVector<int> rightIndices = receiverIndices.mid(splitPoint);

        auto* container = new QWidget(m_snrTab);
        auto* containerLayout = new QHBoxLayout(container);
        containerLayout->setContentsMargins(0, 0, 0, 0);

        QVector<QTreeWidget*> trees;
        QTreeWidget* leftTree = buildSnrTree(leftIndices, receivers, series);
        containerLayout->addWidget(leftTree, 1);
        trees.append(leftTree);

        if (!rightIndices.isEmpty()) {
            QTreeWidget* rightTree = buildSnrTree(rightIndices, receivers, series);
            containerLayout->addWidget(rightTree, 1);
            trees.append(rightTree);
        }

        container->hide();
        m_snrTreeLayout->addWidget(container);
        m_snrStreamContainers.insert(streamOrder, container);
        m_snrStreamTrees.insert(streamOrder, trees);

        // Adding the first item triggers currentIndexChanged(0), which calls
        // onSnrStreamSelected(0) — by then this stream's container is already registered.
        m_snrStreamCombo->addItem(QString("Stream %1").arg(streamOrder), streamOrder);
    }

    if (m_snrStreamCombo->count() == 0) {
        m_snrStreamCombo->setEnabled(false);
        m_snrExpandBtn->setEnabled(false);
    }
}

QTreeWidget* PlotCustomizationDialog::buildSnrTree(const QVector<int>& receiverIndices,
                                                    const QMap<int, QVector<int>>& receivers,
                                                    const QVector<PlotSeriesData>& series)
{
    auto* tree = new QTreeWidget(m_snrTab);
    tree->setHeaderHidden(true);
    tree->setColumnCount(1);
    tree->setRootIsDecorated(true);
    tree->setIndentation(16);
    tree->setFrameShape(QFrame::NoFrame);

    for (int receiverIndex : receiverIndices) {
        QVector<int> indices = receivers.value(receiverIndex);
        std::sort(indices.begin(), indices.end(), [&series](int a, int b) {
            return series[a].channelIndex < series[b].channelIndex;
        });

        auto* rcvrItem = new QTreeWidgetItem(tree);
        rcvrItem->setText(0, QString("RCVR %1").arg(receiverIndex));
        rcvrItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
        if (!indices.isEmpty()) {
            rcvrItem->setForeground(0, series[indices.first()].color);
        }

        int checkedCount = 0;
        for (int idx : indices) {
            const auto& s = series[idx];
            auto* chItem = new QTreeWidgetItem(rcvrItem);
            const QString label = (s.channelIndex < static_cast<int>(UIConstants::kChannelPrefixes.size()))
                ? QString(UIConstants::kChannelPrefixes[s.channelIndex])
                : QString::number(s.channelIndex + 1);
            chItem->setText(0, label);
            chItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            chItem->setCheckState(0, s.visible ? Qt::Checked : Qt::Unchecked);
            chItem->setForeground(0, s.color);
            chItem->setData(0, Qt::UserRole, idx);
            if (s.visible) checkedCount++;
        }

        const Qt::CheckState rcvrState = checkedCount == 0 ? Qt::Unchecked
            : (checkedCount == indices.size() ? Qt::Checked : Qt::PartiallyChecked);
        rcvrItem->setCheckState(0, rcvrState);
    }

    tree->collapseAll();
    connect(tree, &QTreeWidget::itemChanged, this, &PlotCustomizationDialog::onSnrTreeItemChanged);
    return tree;
}

QVector<QTreeWidget*> PlotCustomizationDialog::currentSnrTrees() const
{
    int index = m_snrStreamCombo->currentIndex();
    if (index < 0) return {};

    int streamOrder = m_snrStreamCombo->itemData(index).toInt();
    return m_snrStreamTrees.value(streamOrder);
}

void PlotCustomizationDialog::onSnrStreamSelected(int index)
{
    if (index < 0) return;

    int streamOrder = m_snrStreamCombo->itemData(index).toInt();
    for (auto it = m_snrStreamContainers.begin(); it != m_snrStreamContainers.end(); ++it) {
        it.value()->setVisible(it.key() == streamOrder);
    }

    bool anyCollapsed = false;
    for (QTreeWidget* tree : currentSnrTrees()) {
        for (int i = 0; i < tree->topLevelItemCount(); i++) {
            if (!tree->topLevelItem(i)->isExpanded()) {
                anyCollapsed = true;
                break;
            }
        }
        if (anyCollapsed) break;
    }
    m_snrExpandBtn->setText(anyCollapsed ? tr("Expand All") : tr("Collapse All"));
}

void PlotCustomizationDialog::toggleExpandCollapseSnr()
{
    const QVector<QTreeWidget*> trees = currentSnrTrees();
    if (trees.isEmpty()) return;

    bool anyCollapsed = false;
    for (QTreeWidget* tree : trees) {
        for (int i = 0; i < tree->topLevelItemCount(); i++) {
            if (!tree->topLevelItem(i)->isExpanded()) {
                anyCollapsed = true;
                break;
            }
        }
        if (anyCollapsed) break;
    }

    for (QTreeWidget* tree : trees) {
        if (anyCollapsed) {
            tree->expandAll();
        } else {
            tree->collapseAll();
        }
    }
    m_snrExpandBtn->setText(anyCollapsed ? tr("Collapse All") : tr("Expand All"));
}

void PlotCustomizationDialog::onSnrTreeItemChanged(QTreeWidgetItem* item, int column)
{
    if (m_updatingSnrTree || column != 0) return;

    m_updatingSnrTree = true;
    if (item->parent() == nullptr) {
        // RCVR group checkbox toggled — push the new state down to its L/R/C channels.
        const Qt::CheckState state = item->checkState(0);
        if (state != Qt::PartiallyChecked) {
            for (int c = 0; c < item->childCount(); c++) {
                item->child(c)->setCheckState(0, state);
            }
        }
    } else {
        // Channel checkbox toggled — recompute the parent RCVR group's tristate.
        QTreeWidgetItem* parent = item->parent();
        int checkedCount = 0;
        for (int c = 0; c < parent->childCount(); c++) {
            if (parent->child(c)->checkState(0) == Qt::Checked) checkedCount++;
        }
        const Qt::CheckState parentState = checkedCount == 0 ? Qt::Unchecked
            : (checkedCount == parent->childCount() ? Qt::Checked : Qt::PartiallyChecked);
        parent->setCheckState(0, parentState);
    }
    m_updatingSnrTree = false;
}

void PlotCustomizationDialog::applyChanges()
{
    if (!m_viewModel) return;

    // Apply lock checkboxes. Each checkbox controls both the FrameSyncLock and
    // AccumulatedMissedFrames series for a stream, but only the metric matching the
    // active lock axis view should ever be made visible — the other metric's values
    // are on a different scale and would otherwise be drawn against the wrong axis range.
    const bool showMissedFrames = m_viewModel->lockAxisView() == PlotViewModel::LockAxisView::MissedFrames;
    for (auto* cb : m_lockCheckboxes) {
        bool checked = cb->isChecked();
        const auto& indices = m_lockCheckboxToSeriesIndices.value(cb);
        for (int idx : indices) {
            const auto& s = m_viewModel->seriesAt(idx);
            bool isActiveMetric = (s.metricType == PlotSeriesData::MetricType::AccumulatedMissedFrames)
                == showMissedFrames;
            m_viewModel->setSeriesVisible(idx, checked && isActiveMetric);
        }
    }

    // Apply SNR channel checkboxes
    for (const auto& trees : m_snrStreamTrees) {
        for (QTreeWidget* tree : trees) {
            for (int r = 0; r < tree->topLevelItemCount(); r++) {
                QTreeWidgetItem* rcvrItem = tree->topLevelItem(r);
                for (int c = 0; c < rcvrItem->childCount(); c++) {
                    QTreeWidgetItem* chItem = rcvrItem->child(c);
                    int idx = chItem->data(0, Qt::UserRole).toInt();
                    m_viewModel->setSeriesVisible(idx, chItem->checkState(0) == Qt::Checked);
                }
            }
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
    const QVector<QTreeWidget*> trees = currentSnrTrees();
    if (trees.isEmpty()) return;

    m_updatingSnrTree = true;
    for (QTreeWidget* tree : trees) {
        for (int r = 0; r < tree->topLevelItemCount(); r++) {
            QTreeWidgetItem* rcvrItem = tree->topLevelItem(r);
            rcvrItem->setCheckState(0, Qt::Checked);
            for (int c = 0; c < rcvrItem->childCount(); c++) {
                rcvrItem->child(c)->setCheckState(0, Qt::Checked);
            }
        }
    }
    m_updatingSnrTree = false;
}

void PlotCustomizationDialog::selectNoneSnr()
{
    const QVector<QTreeWidget*> trees = currentSnrTrees();
    if (trees.isEmpty()) return;

    m_updatingSnrTree = true;
    for (QTreeWidget* tree : trees) {
        for (int r = 0; r < tree->topLevelItemCount(); r++) {
            QTreeWidgetItem* rcvrItem = tree->topLevelItem(r);
            rcvrItem->setCheckState(0, Qt::Unchecked);
            for (int c = 0; c < rcvrItem->childCount(); c++) {
                rcvrItem->child(c)->setCheckState(0, Qt::Unchecked);
            }
        }
    }
    m_updatingSnrTree = false;
}
