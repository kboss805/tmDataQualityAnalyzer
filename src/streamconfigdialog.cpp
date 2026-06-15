/**
 * @file streamconfigdialog.cpp
 * @brief Implementation of StreamConfigDialog and its per-mode sub-dialogs.
 */

#include "streamconfigdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "constants.h"
#include "timeextractionwidget.h"
#include "tomlconfighelper.h"

////////////////////////////////////////////////////////////////////////////////
//                       ANONYMOUS-NAMESPACE HELPERS                          //
////////////////////////////////////////////////////////////////////////////////

namespace {

const QRegularExpression kHexRegex("^[0-9A-Fa-f]{1,16}$");

QString periodText(double sec)
{
    if (sec >= 1.0)
        return QString::number(static_cast<int>(sec)) + " s";
    return QString::number(static_cast<int>(sec * 1000)) + " ms";
}

/// Sizes an icon-only button so its SVG icon fills the button, with a
/// transparent, borderless background.
void styleIconButton(QPushButton* button, int size)
{
    button->setFixedSize(size, size);
    button->setIconSize(QSize(size, size));
    button->setStyleSheet("QPushButton { border: none; background: transparent; }");
}

/// Appends a sunken horizontal separator to a vertical layout, with 16px of
/// padding above and below it so it visually divides the dialog body from the
/// button row.
void addSeparator(QVBoxLayout* layout, QWidget* parent)
{
    auto* sep = new QFrame(parent);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);

    auto* wrapper = new QVBoxLayout;
    wrapper->setContentsMargins(0, 16, 0, 16);
    wrapper->addWidget(sep);
    layout->addLayout(wrapper);
}

/// Loads frame sync fields from a TOML file into the provided widgets.
/// Handles both the new BitsPerFrame key and the old WordsInMinorFrame key for
/// backward compatibility with previously saved files.
void loadFrameSyncFromToml(const QString& filename,
                           QLineEdit* syncEdit,
                           QLineEdit* maskEdit,
                           QSpinBox*  bitsSpinBox,
                           QString&   toml_dir)
{
    toml_dir = QFileInfo(filename).absolutePath();
    QSettings cfg(filename, TomlConfigHelper::format());

    QString sync = cfg.value("Frame/FrameSync").toString();
    if (!sync.isEmpty())
        syncEdit->setText(sync.toUpper());

    QString mask = cfg.value("Frame/FrameSyncMask").toString();
    if (!mask.isEmpty())
        maskEdit->setText(mask.toUpper());

    int bits = cfg.value("Frame/BitsPerFrame", 0).toInt();
    if (bits < PCMConstants::kMinFrameLengthBits)
    {
        // Fall back to old WordsInMinorFrame key (words × 16 bits)
        int words = cfg.value("Frame/WordsInMinorFrame", 0).toInt();
        if (words >= 2)
            bits = words * PCMConstants::kCommonWordLen;
    }
    if (bits >= PCMConstants::kMinFrameLengthBits)
        bitsSpinBox->setValue(bits);
}

/// Saves frame sync fields to a TOML file.
void saveFrameSyncToToml(const QString& filename,
                         const QString& syncPattern,
                         const QString& syncMask,
                         int            bitsPerFrame,
                         QString&       toml_dir)
{
    toml_dir = QFileInfo(filename).absolutePath();
    QSettings cfg(filename, TomlConfigHelper::format());
    cfg.beginGroup("Frame");
    cfg.setValue("FrameSync",     syncPattern);
    cfg.setValue("FrameSyncMask", syncMask);
    cfg.setValue("BitsPerFrame",  bitsPerFrame);
    cfg.endGroup();
    cfg.sync();
}

////////////////////////////////////////////////////////////////////////////////
//                        FRAME LOCK SETUP DIALOG                             //
////////////////////////////////////////////////////////////////////////////////

class FrameLockSetupDialog : public QDialog
{
public:
    explicit FrameLockSetupDialog(const StreamConfig& cfg,
                                  const QString& toml_dir,
                                  QWidget* parent = nullptr)
        : QDialog(parent)
        , m_toml_dir(toml_dir)
    {
        setWindowTitle("Frame Sync Lock Setup — " + cfg.label);
        setModal(true);

        // Grid columns: 0=FrameSync/Randomized, 1=SyncMask/DataRate,
        //               2=BitsPerFrame/AveragePeriod, 3=16px gap, 4=LoadBtn, 5=SaveBtn, 6=stretch
        auto* grid = new QGridLayout;
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(4);
        grid->setColumnMinimumWidth(3, 16);
        grid->setColumnStretch(3, 1);

        // Row 0 labels / Row 1 inputs — Frame Sync group
        m_syncPattern = new QLineEdit(this);
        m_syncPattern->setValidator(new QRegularExpressionValidator(kHexRegex, this));
        m_syncPattern->setText(cfg.frameSyncPattern);
        m_syncPattern->setPlaceholderText("e.g. A345CA5C");
        m_syncPattern->setMinimumWidth(110);
        grid->addWidget(new QLabel("Frame Sync"),      0, 0, Qt::AlignHCenter);
        grid->addWidget(m_syncPattern,                 1, 0, Qt::AlignHCenter);

        m_syncMask = new QLineEdit(this);
        m_syncMask->setValidator(new QRegularExpressionValidator(kHexRegex, this));
        m_syncMask->setText(cfg.frameSyncMask);
        m_syncMask->setPlaceholderText("e.g. FFFFFFFF");
        m_syncMask->setMinimumWidth(110);
        grid->addWidget(new QLabel("Frame Sync Mask"), 0, 1, Qt::AlignHCenter);
        grid->addWidget(m_syncMask,                    1, 1, Qt::AlignHCenter);

        m_bitsPerFrame = new QSpinBox(this);
        m_bitsPerFrame->setRange(PCMConstants::kMinFrameLengthBits,
                                 PCMConstants::kMaxFrameLengthBits);
        m_bitsPerFrame->setValue(cfg.bitsInMinorFrame);
        m_bitsPerFrame->setMinimumWidth(80);
        grid->addWidget(new QLabel("Bits Per Frame"),  0, 2, Qt::AlignHCenter);
        grid->addWidget(m_bitsPerFrame,                1, 2, Qt::AlignHCenter);

        // Spacer row between the two groups
        grid->setRowMinimumHeight(2, 8);

        // Row 3 labels / Row 4 inputs — processing group
        m_randomized = new QCheckBox(this);
        m_randomized->setChecked(cfg.randomized);
        grid->addWidget(new QLabel("Randomized"),      3, 0, Qt::AlignHCenter);
        grid->addWidget(m_randomized,                  4, 0, Qt::AlignHCenter);

        m_dataRate = new QDoubleSpinBox(this);
        m_dataRate->setRange(0.0, 1000.0);
        m_dataRate->setDecimals(3);
        m_dataRate->setSingleStep(0.1);
        if (cfg.tmatsDataRateMbps > 0.0)
            m_dataRate->setSpecialValueText(
                QString::number(cfg.tmatsDataRateMbps, 'f', 3));
        else
            m_dataRate->setSpecialValueText("TMATS");
        m_dataRate->setValue(cfg.dataRateMbps);
        m_dataRate->setMinimumWidth(130);
        grid->addWidget(new QLabel("Data Rate (Mbps)"), 3, 1, Qt::AlignHCenter);
        grid->addWidget(m_dataRate,                     4, 1, Qt::AlignHCenter);

        m_sampleRate = new QComboBox(this);
        m_sampleRate->addItem(periodText(UIConstants::kSamplePeriod1s));
        m_sampleRate->addItem(periodText(UIConstants::kSamplePeriod100ms));
        m_sampleRate->addItem(periodText(UIConstants::kSamplePeriod10ms));
        m_sampleRate->setCurrentIndex(cfg.samplePeriodIndex);
        grid->addWidget(new QLabel("Average Period"),  3, 2, Qt::AlignHCenter);
        grid->addWidget(m_sampleRate,                  4, 2, Qt::AlignHCenter);

        auto* loadBtn = new QPushButton(this);
        loadBtn->setIcon(QIcon(":/resources/folder-open.svg"));
        loadBtn->setToolTip("Load frame sync fields from a TOML file");
        styleIconButton(loadBtn, 28);
        grid->addWidget(loadBtn, 1, 4, Qt::AlignVCenter);

        auto* saveBtn = new QPushButton(this);
        saveBtn->setIcon(QIcon(":/resources/floppy-save.svg"));
        saveBtn->setToolTip("Save frame sync fields to a TOML file");
        styleIconButton(saveBtn, 28);
        grid->addWidget(saveBtn, 1, 5, Qt::AlignVCenter);

        connect(loadBtn, &QPushButton::clicked, this, [this]() {
            QString filename = QFileDialog::getOpenFileName(
                this, tr("Load Frame Sync Parameters"), m_toml_dir,
                tr("TOML Files (*.toml);;All Files (*.*)"));
            if (!filename.isEmpty())
                loadFrameSyncFromToml(filename, m_syncPattern, m_syncMask,
                                      m_bitsPerFrame, m_toml_dir);
        });
        connect(saveBtn, &QPushButton::clicked, this, [this]() {
            QString filename = QFileDialog::getSaveFileName(
                this, tr("Save Frame Sync Parameters"), m_toml_dir,
                tr("TOML Files (*.toml);;All Files (*.*)"));
            if (filename.isEmpty()) return;
            if (QFileInfo(filename).suffix().isEmpty()) filename += ".toml";
            saveFrameSyncToToml(filename,
                                m_syncPattern->text().trimmed().toUpper(),
                                m_syncMask->text().trimmed().toUpper(),
                                m_bitsPerFrame->value(),
                                m_toml_dir);
        });

        auto* outer = new QVBoxLayout(this);
        outer->addLayout(grid);
        outer->addStretch(1);

        addSeparator(outer, this);

        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
            if (m_syncPattern->text().trimmed().isEmpty())
            {
                QMessageBox::warning(this, tr("Missing Frame Sync"),
                    tr("A frame sync pattern is required (e.g. FE6B2840)."));
                return;
            }
            accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        auto* bottomLayout = new QHBoxLayout;
        m_applyToAll = new QCheckBox("Apply to all Frame Sync Lock streams", this);
        m_applyToAll->setToolTip("Copy these settings to every other selected "
                                  "stream currently set to Frame Sync Lock mode.");
        bottomLayout->addWidget(m_applyToAll);
        bottomLayout->addStretch(1);
        bottomLayout->addWidget(buttons);
        outer->addLayout(bottomLayout);

        adjustSize();
    }

    QString frameSyncPattern() const { return m_syncPattern->text().trimmed().toUpper(); }
    QString frameSyncMask()    const { return m_syncMask->text().trimmed().toUpper(); }
    int     bitsPerFrame()     const { return m_bitsPerFrame->value(); }
    bool    randomized()        const { return m_randomized->isChecked(); }
    int     samplePeriodIndex() const { return m_sampleRate->currentIndex(); }
    double  dataRateMbps()      const { return m_dataRate->value(); }
    QString lastTomlDir()       const { return m_toml_dir; }
    bool    applyToAll()        const { return m_applyToAll->isChecked(); }

private:
    QLineEdit*      m_syncPattern  = nullptr;
    QLineEdit*      m_syncMask     = nullptr;
    QSpinBox*       m_bitsPerFrame = nullptr;
    QCheckBox*      m_randomized   = nullptr;
    QDoubleSpinBox* m_dataRate     = nullptr;
    QComboBox*      m_sampleRate   = nullptr;
    QCheckBox*      m_applyToAll   = nullptr;
    QString         m_toml_dir;
};

////////////////////////////////////////////////////////////////////////////////
//                         RECEIVER SNR DIALOG                                //
////////////////////////////////////////////////////////////////////////////////

class ReceiverSNRDialog : public QDialog
{
public:
    explicit ReceiverSNRDialog(const StreamConfig& cfg,
                               const QString& toml_dir,
                               QWidget* parent = nullptr)
        : QDialog(parent)
        , m_receiverParamsToml(cfg.receiverParamsToml)
        , m_toml_dir(toml_dir)
    {
        setWindowTitle("Receiver SNR Setup — " + cfg.label);
        setModal(true);

        auto* outer = new QVBoxLayout(this);
        outer->setSpacing(12);

        // ---- Group 1: Frame sync + acquisition settings --------------------
        {
            auto* grid = new QGridLayout;
            grid->setHorizontalSpacing(10);
            grid->setVerticalSpacing(4);

            // Row 0: labels
            grid->addWidget(new QLabel("Frame Sync"),      0, 0, Qt::AlignHCenter);
            grid->addWidget(new QLabel("Frame Sync Mask"), 0, 1, Qt::AlignHCenter);
            grid->addWidget(new QLabel("Bits Per Frame"),  0, 2, Qt::AlignHCenter);

            // Row 1: inputs
            m_syncPattern = new QLineEdit(this);
            m_syncPattern->setValidator(new QRegularExpressionValidator(kHexRegex, this));
            m_syncPattern->setText(cfg.frameSyncPattern);
            m_syncPattern->setPlaceholderText("e.g. FE6B2840");
            m_syncPattern->setMinimumWidth(90);
            grid->addWidget(m_syncPattern, 1, 0, Qt::AlignHCenter);

            m_syncMask = new QLineEdit(this);
            m_syncMask->setValidator(new QRegularExpressionValidator(kHexRegex, this));
            m_syncMask->setText(cfg.frameSyncMask);
            m_syncMask->setPlaceholderText("e.g. FFFFFFFF");
            m_syncMask->setMinimumWidth(90);
            grid->addWidget(m_syncMask, 1, 1, Qt::AlignHCenter);

            m_bitsPerFrame = new QSpinBox(this);
            m_bitsPerFrame->setRange(PCMConstants::kMinFrameLengthBits,
                                     PCMConstants::kMaxFrameLengthBits);
            m_bitsPerFrame->setValue(cfg.bitsInMinorFrame);
            m_bitsPerFrame->setMinimumWidth(80);
            grid->addWidget(m_bitsPerFrame, 1, 2, Qt::AlignHCenter);

            // Row 2: spacer
            grid->setRowMinimumHeight(2, 8);

            // Row 3: labels
            grid->addWidget(new QLabel("Randomized"),       3, 0, Qt::AlignHCenter);
            grid->addWidget(new QLabel("Data Rate (Mbps)"), 3, 1, Qt::AlignHCenter);
            grid->addWidget(new QLabel("Average Period"),   3, 2, Qt::AlignHCenter);

            // Row 4: inputs + buttons
            m_randomized = new QCheckBox(this);
            m_randomized->setChecked(cfg.randomized);
            grid->addWidget(m_randomized, 4, 0, Qt::AlignHCenter | Qt::AlignVCenter);

            m_dataRate = new QDoubleSpinBox(this);
            m_dataRate->setRange(0.0, 1000.0);
            m_dataRate->setDecimals(3);
            m_dataRate->setSingleStep(0.1);
            if (cfg.tmatsDataRateMbps > 0.0)
                m_dataRate->setSpecialValueText(
                    QString::number(cfg.tmatsDataRateMbps, 'f', 3));
            else
                m_dataRate->setSpecialValueText("TMATS");
            m_dataRate->setValue(cfg.dataRateMbps);
            m_dataRate->setMinimumWidth(110);
            grid->addWidget(m_dataRate, 4, 1, Qt::AlignHCenter);

            m_sampleRate = new QComboBox(this);
            m_sampleRate->addItem(periodText(UIConstants::kSamplePeriod1s));
            m_sampleRate->addItem(periodText(UIConstants::kSamplePeriod100ms));
            m_sampleRate->addItem(periodText(UIConstants::kSamplePeriod10ms));
            m_sampleRate->setCurrentIndex(cfg.samplePeriodIndex);
            grid->addWidget(m_sampleRate, 4, 2, Qt::AlignHCenter);

            grid->setColumnMinimumWidth(3, 16);

            auto* loadBtn1 = new QPushButton(this);
            loadBtn1->setIcon(QIcon(":/resources/folder-open.svg"));
            loadBtn1->setToolTip("Load frame sync fields from a TOML file");
            styleIconButton(loadBtn1, 28);
            auto* saveBtn1 = new QPushButton(this);
            saveBtn1->setIcon(QIcon(":/resources/floppy-save.svg"));
            saveBtn1->setToolTip("Save frame sync fields to a TOML file");
            styleIconButton(saveBtn1, 28);
            grid->addWidget(loadBtn1, 1, 4, Qt::AlignVCenter);
            grid->addWidget(saveBtn1, 1, 5, Qt::AlignVCenter);
            grid->setColumnStretch(3, 1);

            connect(loadBtn1, &QPushButton::clicked, this, [this]() {
                QString filename = QFileDialog::getOpenFileName(
                    this, tr("Load Frame Sync Parameters"), m_toml_dir,
                    tr("TOML Files (*.toml);;All Files (*.*)"));
                if (!filename.isEmpty())
                    loadFrameSyncFromToml(filename, m_syncPattern, m_syncMask,
                                         m_bitsPerFrame, m_toml_dir);
            });
            connect(saveBtn1, &QPushButton::clicked, this, [this]() {
                QString filename = QFileDialog::getSaveFileName(
                    this, tr("Save Frame Sync Parameters"), m_toml_dir,
                    tr("TOML Files (*.toml);;All Files (*.*)"));
                if (filename.isEmpty()) return;
                if (QFileInfo(filename).suffix().isEmpty()) filename += ".toml";
                saveFrameSyncToToml(filename,
                                    m_syncPattern->text().trimmed().toUpper(),
                                    m_syncMask->text().trimmed().toUpper(),
                                    m_bitsPerFrame->value(),
                                    m_toml_dir);
            });

            outer->addLayout(grid);
        }

        // ---- Separator ------------------------------------------------------
        {
            auto* sep = new QFrame(this);
            sep->setFrameShape(QFrame::HLine);
            sep->setFrameShadow(QFrame::Sunken);
            outer->addWidget(sep);
        }

        // ---- Group 2: Receiver calibration parameters ----------------------
        {
            auto* grid = new QGridLayout;
            grid->setHorizontalSpacing(10);
            grid->setVerticalSpacing(4);

            // Row 0: labels
            grid->addWidget(new QLabel("Polarity"),      0, 0, Qt::AlignHCenter);
            grid->addWidget(new QLabel("Slope"),         0, 1, Qt::AlignHCenter);
            grid->addWidget(new QLabel("Scale (dB/V)"),  0, 2, Qt::AlignHCenter);

            // Row 1: inputs
            m_polarity = new QComboBox(this);
            m_polarity->addItem("Positive");
            m_polarity->addItem("Negative");
            m_polarity->setCurrentIndex(cfg.polarityIndex);
            grid->addWidget(m_polarity, 1, 0, Qt::AlignHCenter);

            m_slope = new QComboBox(this);
            m_slope->addItem("±10 V");
            m_slope->addItem("±5 V");
            m_slope->addItem("0–10 V");
            m_slope->addItem("0–5 V");
            m_slope->setCurrentIndex(cfg.slopeIndex);
            grid->addWidget(m_slope, 1, 1, Qt::AlignHCenter);

            m_scale = new QDoubleSpinBox(this);
            m_scale->setRange(0.001, 999.999);
            m_scale->setDecimals(3);
            m_scale->setValue(cfg.scaleDdBPerV);
            m_scale->setMinimumWidth(100);
            grid->addWidget(m_scale, 1, 2, Qt::AlignHCenter);

            // Row 2: spacer
            grid->setRowMinimumHeight(2, 8);

            // Row 3: labels
            grid->addWidget(new QLabel("Num Receivers"),     3, 0, Qt::AlignHCenter);
            grid->addWidget(new QLabel("Receiver Channels"), 3, 1, Qt::AlignHCenter);

            // Row 4: inputs + buttons
            m_numReceivers = new QSpinBox(this);
            m_numReceivers->setRange(1, 100);
            m_numReceivers->setValue(cfg.numReceivers);
            grid->addWidget(m_numReceivers, 4, 0, Qt::AlignHCenter);

            m_receiverChannels = new QSpinBox(this);
            m_receiverChannels->setRange(1, 100);
            m_receiverChannels->setValue(cfg.receiverChannels);
            grid->addWidget(m_receiverChannels, 4, 1, Qt::AlignHCenter);

            grid->setColumnMinimumWidth(3, 16);

            auto* loadBtn2 = new QPushButton(this);
            loadBtn2->setIcon(QIcon(":/resources/folder-open.svg"));
            loadBtn2->setToolTip("Load receiver parameters from a TOML file");
            styleIconButton(loadBtn2, 28);
            auto* saveBtn2 = new QPushButton(this);
            saveBtn2->setIcon(QIcon(":/resources/floppy-save.svg"));
            saveBtn2->setToolTip("Save receiver parameters to a TOML file");
            styleIconButton(saveBtn2, 28);
            grid->addWidget(loadBtn2, 1, 4, Qt::AlignVCenter);
            grid->addWidget(saveBtn2, 1, 5, Qt::AlignVCenter);
            grid->setColumnStretch(3, 1);

            connect(loadBtn2, &QPushButton::clicked, this, [this]() {
                QString filename = QFileDialog::getOpenFileName(
                    this, tr("Load Receiver Parameters"), m_toml_dir,
                    tr("TOML Files (*.toml);;All Files (*.*)"));
                if (filename.isEmpty()) return;
                m_toml_dir = QFileInfo(filename).absolutePath();
                m_receiverParamsToml = filename;
                updateReceiverParamsLabel();
                QSettings cfg(filename, TomlConfigHelper::format());
                m_polarity->setCurrentIndex(
                    cfg.value("Parameters/Polarity", UIConstants::kDefaultPolarityIndex).toInt());
                m_slope->setCurrentIndex(
                    cfg.value("Parameters/Slope", UIConstants::kDefaultSlopeIndex).toInt());
                double scale = cfg.value("Parameters/Scale",
                                         PCMConstants::kDefaultScaleDdBPerV).toDouble();
                if (scale > 0.0) m_scale->setValue(scale);
                int nr = cfg.value("Parameters/NumReceivers",
                                    PCMConstants::kDefaultNumReceivers).toInt();
                if (nr > 0) m_numReceivers->setValue(nr);
                int rc = cfg.value("Parameters/ReceiverChannels",
                                    PCMConstants::kDefaultReceiverChannels).toInt();
                if (rc > 0) m_receiverChannels->setValue(rc);
            });
            connect(saveBtn2, &QPushButton::clicked, this, [this]() {
                QString filename = QFileDialog::getSaveFileName(
                    this, tr("Save Receiver Parameters"), m_toml_dir,
                    tr("TOML Files (*.toml);;All Files (*.*)"));
                if (filename.isEmpty()) return;
                if (QFileInfo(filename).suffix().isEmpty()) filename += ".toml";
                m_toml_dir = QFileInfo(filename).absolutePath();
                QSettings cfg(filename, TomlConfigHelper::format());
                cfg.beginGroup("Parameters");
                cfg.setValue("Polarity",         m_polarity->currentIndex());
                cfg.setValue("Slope",            m_slope->currentIndex());
                cfg.setValue("Scale",            m_scale->value());
                cfg.setValue("NumReceivers",     m_numReceivers->value());
                cfg.setValue("ReceiverChannels", m_receiverChannels->value());
                cfg.endGroup();
                cfg.sync();
            });

            outer->addLayout(grid);

            auto* paramsRow = new QHBoxLayout;
            paramsRow->addWidget(new QLabel("Receiver Parameters File:"));
            m_receiverParamsLabel = new QLabel(this);
            paramsRow->addWidget(m_receiverParamsLabel);
            paramsRow->addStretch(1);
            outer->addLayout(paramsRow);
            updateReceiverParamsLabel();
        }

        addSeparator(outer, this);

        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
            if (m_syncPattern->text().trimmed().isEmpty())
            {
                QMessageBox::warning(this, tr("Missing Frame Sync"),
                    tr("A frame sync pattern is required (e.g. FE6B2840)."));
                return;
            }
            accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        auto* bottomLayout = new QHBoxLayout;
        m_applyToAll = new QCheckBox("Apply to all Receiver SNR streams", this);
        m_applyToAll->setToolTip("Copy these settings to every other selected "
                                  "stream currently set to Receiver SNR mode.");
        bottomLayout->addWidget(m_applyToAll);
        bottomLayout->addStretch(1);
        bottomLayout->addWidget(buttons);
        outer->addLayout(bottomLayout);

        adjustSize();
    }

    QString frameSyncPattern()   const { return m_syncPattern->text().trimmed().toUpper(); }
    QString frameSyncMask()      const { return m_syncMask->text().trimmed().toUpper(); }
    int     bitsPerFrame()       const { return m_bitsPerFrame->value(); }
    bool    randomized()         const { return m_randomized->isChecked(); }
    int     samplePeriodIndex()    const { return m_sampleRate->currentIndex(); }
    double  dataRateMbps()       const { return m_dataRate->value(); }
    int     polarityIndex()      const { return m_polarity->currentIndex(); }
    int     slopeIndex()         const { return m_slope->currentIndex(); }
    double  scaleDdBPerV()       const { return m_scale->value(); }
    int     numReceivers()       const { return m_numReceivers->value(); }
    int     receiverChannels()   const { return m_receiverChannels->value(); }
    QString receiverParamsToml() const { return m_receiverParamsToml; }
    QString lastTomlDir()        const { return m_toml_dir; }
    bool    applyToAll()         const { return m_applyToAll->isChecked(); }

private:
    void updateReceiverParamsLabel()
    {
        if (m_receiverParamsToml.isEmpty())
            m_receiverParamsLabel->setText("<span style='color: gray;'>(none — using default word map)</span>");
        else
            m_receiverParamsLabel->setText(QFileInfo(m_receiverParamsToml).fileName());
    }

    QLineEdit*      m_syncPattern      = nullptr;
    QLineEdit*      m_syncMask         = nullptr;
    QSpinBox*       m_bitsPerFrame     = nullptr;
    QCheckBox*      m_randomized       = nullptr;
    QDoubleSpinBox* m_dataRate         = nullptr;
    QComboBox*      m_sampleRate       = nullptr;
    QComboBox*      m_polarity         = nullptr;
    QComboBox*      m_slope            = nullptr;
    QDoubleSpinBox* m_scale            = nullptr;
    QSpinBox*       m_numReceivers     = nullptr;
    QSpinBox*       m_receiverChannels = nullptr;
    QCheckBox*      m_applyToAll       = nullptr;
    QLabel*         m_receiverParamsLabel = nullptr;
    QString         m_receiverParamsToml;
    QString         m_toml_dir;
};

} // namespace

////////////////////////////////////////////////////////////////////////////////
//                         STREAM CONFIG DIALOG                               //
////////////////////////////////////////////////////////////////////////////////

StreamConfigDialog::StreamConfigDialog(const QVector<StreamConfig>& configs,
                                       const QString& toml_dir,
                                       const QStringList& time_channels,
                                       int time_channel_index,
                                       const TimeFields& start_time,
                                       const TimeFields& stop_time,
                                       bool extract_all_time,
                                       QWidget* parent)
    : QDialog(parent)
    , m_configs(configs)
    , m_toml_dir(toml_dir)
{
    setWindowTitle("Configure Streams");
    setModal(true);
    resize(600, 600);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(10);

    // Time channel row
    {
        auto* hl = new QHBoxLayout;
        hl->setContentsMargins(0, 4, 0, 4);
        hl->addWidget(new QLabel("Time Channel:"));
        m_time_channel_combo = new QComboBox(this);
        m_time_channel_combo->addItems(time_channels);
        m_time_channel_combo->setEnabled(!time_channels.isEmpty());
        if (!time_channels.isEmpty())
        {
            int idx = (time_channel_index > 0) ? time_channel_index - 1 : 0;
            m_time_channel_combo->setCurrentIndex(idx);
        }
        hl->addWidget(m_time_channel_combo);
        hl->addStretch(1);
        layout->addLayout(hl);
    }

    m_table = new QTableWidget(this);
    layout->addWidget(m_table, 1);

    buildTable();

    m_time_widget = new TimeExtractionWidget(this);
    m_time_widget->setExtractAllTime(extract_all_time);
    m_time_widget->fillTimes(start_time, stop_time);
    m_time_widget->setAllEnabled(true);
    layout->addWidget(m_time_widget);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_ok_btn = buttons->button(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &StreamConfigDialog::validateAndAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // m_ok_btn was nullptr during buildTable(), so call once now to reflect actual state.
    updateOkButton();
}

void StreamConfigDialog::buildTable()
{
    constexpr int kColProcess = 0;
    constexpr int kColChannel = 1;
    constexpr int kColMode    = 2;
    constexpr int kColSetup   = 3;
    constexpr int kColReady   = 4;
    constexpr int kRowHeight  = 52;

    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({"Process", "Channel", "Mode", "Setup", "Ready"});
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(kRowHeight);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setSectionResizeMode(kColMode, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kColProcess, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(kColChannel, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(kColSetup,   QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(kColReady,   QHeaderView::ResizeToContents);

    m_table->setRowCount(static_cast<int>(m_configs.size()));
    m_rows.resize(static_cast<int>(m_configs.size()));

    auto centeredWidget = [](QWidget* inner) -> QWidget* {
        auto* container = new QWidget;
        container->setStyleSheet("background-color: transparent;");
        auto* l = new QHBoxLayout(container);
        l->setContentsMargins(4, 2, 4, 2);
        l->setAlignment(Qt::AlignCenter);
        l->addWidget(inner);
        return container;
    };

    auto paddedWidget = [](QWidget* inner) -> QWidget* {
        auto* container = new QWidget;
        container->setStyleSheet("background-color: transparent;");
        auto* l = new QHBoxLayout(container);
        l->setContentsMargins(4, 2, 4, 2);
        l->addWidget(inner);
        return container;
    };

    for (int row = 0; row < m_configs.size(); row++)
    {
        const StreamConfig& cfg = m_configs[row];
        RowWidgets& w = m_rows[row];

        // Seed stored values from the incoming config
        w.frameSyncPattern  = cfg.frameSyncPattern;
        w.frameSyncMask     = cfg.frameSyncMask;
        w.bitsInFrame       = cfg.bitsInMinorFrame;
        w.randomized        = cfg.randomized;
        w.samplePeriodIndex   = cfg.samplePeriodIndex;
        w.dataRateMbps      = cfg.dataRateMbps;
        w.polarityIndex     = cfg.polarityIndex;
        w.slopeIndex        = cfg.slopeIndex;
        w.scaleDdBPerV      = cfg.scaleDdBPerV;
        w.numReceivers      = cfg.numReceivers;
        w.receiverChannels  = cfg.receiverChannels;
        w.receiverParamsToml  = cfg.receiverParamsToml;
        w.lastConfiguredMode  = cfg.mode;

        // Process checkbox
        w.process = new QCheckBox(m_table);
        w.process->setChecked(cfg.process);
        m_table->setCellWidget(row, kColProcess, centeredWidget(w.process));

        // Channel label
        auto* channel_item = new QTableWidgetItem(cfg.label);
        channel_item->setFlags(Qt::ItemIsEnabled);
        channel_item->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, kColChannel, channel_item);

        // Mode combo
        w.mode = new QComboBox(m_table);
        w.mode->addItem("Receiver SNR");
        w.mode->addItem("Frame Sync Lock");
        w.mode->setCurrentIndex(cfg.mode == StreamMode::FrameSyncLockStats ? 1 : 0);
        m_table->setCellWidget(row, kColMode, paddedWidget(w.mode));

        // Gear button
        w.gearBtn = new QPushButton(m_table);
        w.gearBtn->setIcon(QIcon(":/resources/gear.svg"));
        w.gearBtn->setToolTip("Configure this stream");
        styleIconButton(w.gearBtn, 28);
        w.gearBtn->setEnabled(cfg.process);
        m_table->setCellWidget(row, kColSetup, centeredWidget(w.gearBtn));

        // Ready label
        w.readyLabel = new QLabel(m_table);
        w.readyLabel->setTextFormat(Qt::RichText);
        w.readyLabel->setAlignment(Qt::AlignCenter);
        m_table->setCellWidget(row, kColReady, centeredWidget(w.readyLabel));

        updateReadyIcon(row);

        connect(w.process, &QCheckBox::toggled, this, [this, row](bool) {
            updateReadyIcon(row);
        });
        connect(w.mode, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, row](int) { updateReadyIcon(row); });
        connect(w.gearBtn, &QPushButton::clicked,
                this, [this, row]() { openGearDialog(row); });
    }
}

void StreamConfigDialog::updateReadyIcon(int row)
{
    RowWidgets& w = m_rows[row];
    bool checked = w.process->isChecked();
    w.gearBtn->setEnabled(checked);
    w.mode->setEnabled(checked);

    if (!checked)
    {
        w.readyLabel->setText("<span style='color: gray; font-size: 28px;'>✗</span>");
        w.readyLabel->setToolTip(QString());
    }
    else if (!w.gearConfirmed)
    {
        w.readyLabel->setText("<span style='color: red; font-size: 28px;'>✗</span>");
        w.readyLabel->setToolTip("Click the gear icon to configure this stream.");
    }
    else
    {
        bool frame_ok = !w.frameSyncPattern.isEmpty();

        if (frame_ok)
        {
            w.readyLabel->setText("<span style='color: green; font-size: 28px;'>✓</span>");
            w.readyLabel->setToolTip(QString());
        }
        else
        {
            w.readyLabel->setText("<span style='color: red; font-size: 28px;'>✗</span>");
            w.readyLabel->setToolTip("A frame sync pattern is required.");
        }
    }

    updateOkButton();
}

void StreamConfigDialog::updateOkButton()
{
    if (!m_ok_btn)
        return;

    bool any_ready = false;
    for (const RowWidgets& w : m_rows)
    {
        if (!w.process->isChecked() || !w.gearConfirmed)
            continue;
        bool frame_ok = !w.frameSyncPattern.isEmpty();
        if (frame_ok)
        {
            any_ready = true;
            break;
        }
    }
    m_ok_btn->setEnabled(any_ready);
}

void StreamConfigDialog::openGearDialog(int row)
{
    RowWidgets& w = m_rows[row];
    const bool is_frame_sync_lock = (w.mode->currentIndex() == 1);
    const StreamMode current_mode = is_frame_sync_lock
        ? StreamMode::FrameSyncLockStats : StreamMode::ReceiverChannelInfo;

    // Populate temp from stored values, preserving tmatsDataRateMbps for display.
    StreamConfig temp = m_configs[row];
    temp.frameSyncPattern   = w.frameSyncPattern;
    temp.frameSyncMask      = w.frameSyncMask;
    temp.bitsInMinorFrame   = w.bitsInFrame;
    temp.randomized         = w.randomized;
    temp.samplePeriodIndex    = w.samplePeriodIndex;
    temp.dataRateMbps       = w.dataRateMbps;
    temp.polarityIndex      = w.polarityIndex;
    temp.slopeIndex         = w.slopeIndex;
    temp.scaleDdBPerV       = w.scaleDdBPerV;
    temp.numReceivers       = w.numReceivers;
    temp.receiverChannels   = w.receiverChannels;
    temp.receiverParamsToml = w.receiverParamsToml;

    // If the user changed modes since last configure, reset frame sync fields to
    // this mode's defaults so the sub-dialog pre-fills with sensible values.
    if (current_mode != w.lastConfiguredMode)
    {
        temp.frameSyncMask = PCMConstants::kDefaultFrameSyncMask;
        if (is_frame_sync_lock)
        {
            temp.frameSyncPattern  = PCMConstants::kDefaultFrameSyncLockPattern;
            temp.bitsInMinorFrame  = PCMConstants::kDefaultFrameSyncLockBits;
            temp.receiverParamsToml.clear();
        }
        else
        {
            temp.frameSyncPattern = PCMConstants::kDefaultReceiverSNRPattern;
            temp.bitsInMinorFrame = PCMConstants::kDefaultReceiverSNRBits;
        }
    }

    if (is_frame_sync_lock)
    {
        FrameLockSetupDialog dlg(temp, m_toml_dir, this);
        if (dlg.exec() == QDialog::Accepted)
        {
            w.frameSyncPattern   = dlg.frameSyncPattern();
            w.frameSyncMask      = dlg.frameSyncMask();
            w.bitsInFrame        = dlg.bitsPerFrame();
            w.randomized         = dlg.randomized();
            w.samplePeriodIndex    = dlg.samplePeriodIndex();
            w.dataRateMbps       = dlg.dataRateMbps();
            m_toml_dir           = dlg.lastTomlDir();
            w.lastConfiguredMode = StreamMode::FrameSyncLockStats;
            w.gearConfirmed      = true;
            updateReadyIcon(row);
            
            if (dlg.applyToAll())
            {
                for (int i = 0; i < m_rows.size(); i++)
                {
                    if (i == row || !m_rows[i].process->isChecked()) continue;
                    if (m_rows[i].mode->currentIndex() != 1) continue; // different mode: leave unchanged
                    RowWidgets& rw = m_rows[i];
                    rw.frameSyncPattern   = dlg.frameSyncPattern();
                    rw.frameSyncMask      = dlg.frameSyncMask();
                    rw.bitsInFrame        = dlg.bitsPerFrame();
                    rw.randomized         = dlg.randomized();
                    rw.samplePeriodIndex  = dlg.samplePeriodIndex();
                    rw.dataRateMbps       = dlg.dataRateMbps();
                    rw.lastConfiguredMode = StreamMode::FrameSyncLockStats;
                    rw.gearConfirmed      = true;
                    updateReadyIcon(i);
                }
            }
        }
    }
    else
    {
        ReceiverSNRDialog dlg(temp, m_toml_dir, this);
        if (dlg.exec() == QDialog::Accepted)
        {
            w.frameSyncPattern   = dlg.frameSyncPattern();
            w.frameSyncMask      = dlg.frameSyncMask();
            w.bitsInFrame        = dlg.bitsPerFrame();
            w.randomized         = dlg.randomized();
            w.samplePeriodIndex    = dlg.samplePeriodIndex();
            w.dataRateMbps       = dlg.dataRateMbps();
            w.polarityIndex      = dlg.polarityIndex();
            w.slopeIndex         = dlg.slopeIndex();
            w.scaleDdBPerV       = dlg.scaleDdBPerV();
            w.numReceivers       = dlg.numReceivers();
            w.receiverChannels   = dlg.receiverChannels();
            w.receiverParamsToml = dlg.receiverParamsToml();
            m_toml_dir           = dlg.lastTomlDir();
            w.lastConfiguredMode = StreamMode::ReceiverChannelInfo;
            w.gearConfirmed      = true;
            updateReadyIcon(row);
            
            if (dlg.applyToAll())
            {
                for (int i = 0; i < m_rows.size(); i++)
                {
                    if (i == row || !m_rows[i].process->isChecked()) continue;
                    if (m_rows[i].mode->currentIndex() != 0) continue; // different mode: leave unchanged
                    RowWidgets& rw = m_rows[i];
                    rw.frameSyncPattern   = dlg.frameSyncPattern();
                    rw.frameSyncMask      = dlg.frameSyncMask();
                    rw.bitsInFrame        = dlg.bitsPerFrame();
                    rw.randomized         = dlg.randomized();
                    rw.samplePeriodIndex  = dlg.samplePeriodIndex();
                    rw.dataRateMbps       = dlg.dataRateMbps();
                    rw.polarityIndex      = dlg.polarityIndex();
                    rw.slopeIndex         = dlg.slopeIndex();
                    rw.scaleDdBPerV       = dlg.scaleDdBPerV();
                    rw.numReceivers       = dlg.numReceivers();
                    rw.receiverChannels   = dlg.receiverChannels();
                    rw.receiverParamsToml = dlg.receiverParamsToml();
                    rw.lastConfiguredMode = StreamMode::ReceiverChannelInfo;
                    rw.gearConfirmed      = true;
                    updateReadyIcon(i);
                }
            }
        }
    }
}

void StreamConfigDialog::validateAndAccept()
{
    bool any_checked = false;
    for (const RowWidgets& w : m_rows)
        if (w.process->isChecked()) { any_checked = true; break; }
    if (!any_checked)
    {
        QMessageBox::warning(this, tr("No Streams Selected"),
            tr("Check at least one stream to process."));
        return;
    }

    for (int row = 0; row < m_rows.size(); row++)
    {
        const RowWidgets& w = m_rows[row];
        if (!w.process->isChecked())
            continue;

        const QString channel = m_configs[row].label;

        if (w.frameSyncPattern.isEmpty())
        {
            QMessageBox::warning(this, tr("Missing Frame Sync"),
                channel + tr(": a frame sync pattern is required. "
                             "Click the gear icon to configure this stream."));
            return;
        }
    }
    accept();
}

int StreamConfigDialog::timeChannelIndex() const
{
    if (!m_time_channel_combo || m_time_channel_combo->count() == 0)
        return 0;
    return m_time_channel_combo->currentIndex() + 1;
}

bool StreamConfigDialog::extractAllTime() const
{
    return m_time_widget ? m_time_widget->extractAllTime() : true;
}

QString StreamConfigDialog::startTimeText() const
{
    return m_time_widget ? m_time_widget->startTimeText() : QString();
}

QString StreamConfigDialog::stopTimeText() const
{
    return m_time_widget ? m_time_widget->stopTimeText() : QString();
}

QVector<StreamConfig> StreamConfigDialog::configs() const
{
    QVector<StreamConfig> result = m_configs;
    for (int row = 0; row < m_rows.size() && row < result.size(); row++)
    {
        const RowWidgets& w = m_rows[row];
        result[row].process           = w.process->isChecked();
        result[row].mode              = (w.mode->currentIndex() == 1)
            ? StreamMode::FrameSyncLockStats : StreamMode::ReceiverChannelInfo;
        result[row].frameSyncPattern  = w.frameSyncPattern;
        result[row].frameSyncMask     = w.frameSyncMask;
        result[row].bitsInMinorFrame  = w.bitsInFrame;
        result[row].randomized        = w.randomized;
        result[row].samplePeriodIndex   = w.samplePeriodIndex;
        result[row].dataRateMbps      = w.dataRateMbps;
        result[row].polarityIndex     = w.polarityIndex;
        result[row].slopeIndex        = w.slopeIndex;
        result[row].scaleDdBPerV      = w.scaleDdBPerV;
        result[row].numReceivers      = w.numReceivers;
        result[row].receiverChannels  = w.receiverChannels;
        result[row].receiverParamsToml = w.receiverParamsToml;
    }
    return result;
}
