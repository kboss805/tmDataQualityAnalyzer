#ifndef PLOTCUSTOMIZATIONDIALOG_H
#define PLOTCUSTOMIZATIONDIALOG_H

#include <QColor>
#include <QDialog>
#include <QMap>
#include <QVector>

class QTabWidget;
class QVBoxLayout;
class QComboBox;
class QScrollArea;
class QCheckBox;
class QLineEdit;
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

    /// One Lock-tab row's widgets and pending edits, kept together so they can't
    /// drift out of sync the way five parallel QMaps keyed by the same checkbox
    /// could (a row's checkbox, series indices, name edit, swatch, and pending
    /// color are all set together in populateData() and read together in
    /// applyChanges() — there was never a reason for them to live apart).
    struct LockRow
    {
        QCheckBox* checkbox = nullptr;
        /// Stable ids (PlotSeriesData::id) of the series this row's checkbox
        /// controls — typically both the FrameSyncLock and AccumulatedMissedFrames
        /// series for the stream. Stored as ids, not raw indices: applyChanges()
        /// resolves them via PlotViewModel::indexOfSeriesId() so an async
        /// reprocess/import that reorders the series list while this modal dialog
        /// is open can't make the edits land on the wrong series (or read out of
        /// range in seriesAt()).
        QVector<int> seriesIds;
        QLineEdit* nameEdit = nullptr;
        QPushButton* swatch = nullptr;
        QColor color; ///< Pending color chosen from the swatch; applied to the ViewModel on OK.
    };
    QVector<LockRow> m_lockRows;

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
