#ifndef PLOTCUSTOMIZATIONDIALOG_H
#define PLOTCUSTOMIZATIONDIALOG_H

#include <QDialog>
#include <QMap>
#include <QVector>
#include <QPair>

class QTabWidget;
class QVBoxLayout;
class QComboBox;
class QGridLayout;
class QScrollArea;
class QCheckBox;
class PlotViewModel;

/**
 * @brief Dialog allowing the user to customize which data series are visible on the plot.
 *
 * Provides a tabbed interface for Frame Sync Lock data and Receiver SNR data,
 * handling large densities of channels cleanly.
 */
class PlotCustomizationDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PlotCustomizationDialog(PlotViewModel* viewModel, QWidget* parent = nullptr);

private slots:
    void applyChanges();
    void onSnrStreamSelected(int index);
    void selectAllLock();
    void selectNoneLock();
    void selectAllSnr();
    void selectNoneSnr();

private:
    void setupUi();
    void populateData();
    void clearSnrGrid();

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
    QWidget* m_snrGridContainer;
    QGridLayout* m_snrGridLayout;
    
    // Map of Stream Order -> List of <Checkbox, SeriesIndex>
    QMap<int, QVector<QPair<QCheckBox*, int>>> m_snrStreamGroups;
};

#endif // PLOTCUSTOMIZATIONDIALOG_H
