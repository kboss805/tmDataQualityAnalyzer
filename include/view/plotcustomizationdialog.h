#ifndef PLOTCUSTOMIZATIONDIALOG_H
#define PLOTCUSTOMIZATIONDIALOG_H

#include <QDialog>
#include <QMap>
#include <QVector>

class QTabWidget;
class QVBoxLayout;
class QComboBox;
class QScrollArea;
class QCheckBox;
class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;
class PlotViewModel;
struct PlotSeriesData;

/**
 * @brief Dialog allowing the user to customize which data series are visible on the plot.
 *
 * Provides a tabbed interface for Frame Sync Lock data and Receiver SNR data,
 * handling large densities of channels cleanly.
 */
class PlotCustomizationDialog : public QDialog
{
    Q_OBJECT
    friend class TestPlotCustomizationDialog;

public:
    explicit PlotCustomizationDialog(PlotViewModel* viewModel, QWidget* parent = nullptr);

private slots:
    void applyChanges();
    void onSnrStreamSelected(int index);
    void selectAllLock();
    void selectNoneLock();
    void selectAllSnr();
    void selectNoneSnr();
    void toggleExpandCollapseSnr();
    void onSnrTreeItemChanged(QTreeWidgetItem* item, int column);

private:
    void setupUi();
    void populateData();
    /// Builds a tree containing the given RCVR groups (with nested L/R/C channel checkboxes).
    QTreeWidget* buildSnrTree(const QVector<int>& receiverIndices,
                              const QMap<int, QVector<int>>& receivers,
                              const QVector<PlotSeriesData>& series);
    /// @return The RCVR/channel trees (one or two columns) for the currently selected stream.
    QVector<QTreeWidget*> currentSnrTrees() const;

    PlotViewModel* m_viewModel;

    QTabWidget* m_tabWidget;

    // Lock Tab UI
    QWidget* m_lockTab;
    QVBoxLayout* m_lockListLayout;
    QVector<QCheckBox*> m_lockCheckboxes;

    // Maps a lock checkbox to the list of series indices it controls
    // (typically both the FrameSyncLock and AccumulatedMissedFrames series for that stream)
    QMap<QCheckBox*, QVector<int>> m_lockCheckboxToSeriesIndices;

    // SNR Tab UI
    QWidget* m_snrTab;
    QComboBox* m_snrStreamCombo;
    QVBoxLayout* m_snrTreeLayout;
    QPushButton* m_snrExpandBtn;

    // Map of Stream Order -> trees of RCVR groups (one or two columns), each with nested L/R/C channel checkboxes
    QMap<int, QVector<QTreeWidget*>> m_snrStreamTrees;

    // Map of Stream Order -> container widget holding the tree column(s) for that stream
    QMap<int, QWidget*> m_snrStreamContainers;

    bool m_updatingSnrTree = false; ///< Guard against recursive itemChanged signals.
};

#endif // PLOTCUSTOMIZATIONDIALOG_H
