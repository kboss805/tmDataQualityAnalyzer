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

QString receiverSNRSampleRateText(int hz)
{
    int window_ms = (hz > 0) ? (1000 / hz) : 0;
    return QString::number(hz) + " Hz  (" + QString::number(window_ms) + " ms avg)";
}

QString frameSyncSampleRateText(int hz)
{
    int window_ms = (hz > 0) ? (1000 / hz) : 0;
    return "Avg window: " + QString::number(window_ms) + " ms  (" + QString::number(hz) + " Hz)";
}

/// Creates a label+widget pair stacked vertically and adds it to @p layout.
void addLabeledWidget(QHBoxLayout* layout, const QString& label, QWidget* widget)
{
    auto* col = new QVBoxLayout;
    col->setSpacing(3);
    col->addWidget(new QLabel(label));
    col->addWidget(widget);
    layout->addLayout(col);
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

        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(14);

        // --- Row 1: Frame Sync, Sync Mask, Bits Per Frame, Load, Save ---
        {
            auto* row = new QWidget(this);
            auto* hl  = new QHBoxLayout(row);
            hl->setContentsMargins(0, 0, 0, 0);
            hl->setSpacing(10);

            m_syncPattern = new QLineEdit(this);
            m_syncPattern->setValidator(new QRegularExpressionValidator(kHexRegex, this));
            m_syncPattern->setText(cfg.frameSyncPattern);
            m_syncPattern->setPlaceholderText("e.g. A345CA5C");
            m_syncPattern->setMinimumWidth(110);
            addLabeledWidget(hl, "Frame Sync", m_syncPattern);

            m_syncMask = new QLineEdit(this);
            m_syncMask->setValidator(new QRegularExpressionValidator(kHexRegex, this));
            m_syncMask->setText(cfg.frameSyncMask);
            m_syncMask->setPlaceholderText("e.g. FFFFFFFF");
            m_syncMask->setMinimumWidth(110);
            addLabeledWidget(hl, "Frame Sync Mask", m_syncMask);

            m_bitsPerFrame = new QSpinBox(this);
            m_bitsPerFrame->setRange(PCMConstants::kMinFrameLengthBits,
                                     PCMConstants::kMaxFrameLengthBits);
            m_bitsPerFrame->setValue(cfg.bitsInMinorFrame);
            m_bitsPerFrame->setMinimumWidth(80);
            addLabeledWidget(hl, "Bits Per Frame", m_bitsPerFrame);

            auto* btnCol = new QVBoxLayout;
            btnCol->setSpacing(2);
            btnCol->addSpacing(18);
            auto* loadBtn = new QPushButton(this);
            loadBtn->setIcon(QIcon(":/resources/folder-open.svg"));
            loadBtn->setToolTip("Load frame sync fields from a TOML file");
            loadBtn->setFixedSize(28, 28);
            auto* saveBtn = new QPushButton(this);
            saveBtn->setIcon(QIcon(":/resources/floppy-save.svg"));
            saveBtn->setToolTip("Save frame sync fields to a TOML file");
            saveBtn->setFixedSize(28, 28);
            btnCol->addWidget(loadBtn);
            btnCol->addWidget(saveBtn);
            btnCol->addStretch();
            hl->addLayout(btnCol);
            hl->addStretch(1);

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

            layout->addWidget(row);
        }

        // --- Row 2: Randomized, Data Rate, Sample Rate ---
        {
            auto* row = new QWidget(this);
            auto* hl  = new QHBoxLayout(row);
            hl->setContentsMargins(0, 0, 0, 0);
            hl->setSpacing(16);

            m_randomized = new QCheckBox(this);
            m_randomized->setChecked(cfg.randomized);
            {
                auto* col = new QVBoxLayout;
                col->setSpacing(3);
                col->addWidget(new QLabel("Randomized"));
                col->addWidget(m_randomized);
                hl->addLayout(col);
            }

            m_dataRate = new QDoubleSpinBox(this);
            m_dataRate->setRange(0.0, 1000.0);
            m_dataRate->setDecimals(3);
            m_dataRate->setSingleStep(0.1);
            m_dataRate->setSuffix(" Mbps");
            if (cfg.tmatsDataRateMbps > 0.0)
                m_dataRate->setSpecialValueText(
                    QString("Auto (%1 Mbps)").arg(cfg.tmatsDataRateMbps, 0, 'f', 3));
            else
                m_dataRate->setSpecialValueText("Auto (TMATS)");
            m_dataRate->setValue(cfg.dataRateMbps);
            m_dataRate->setMinimumWidth(130);
            addLabeledWidget(hl, "Data Rate (Mbps)", m_dataRate);

            m_sampleRate = new QComboBox(this);
            m_sampleRate->addItem(frameSyncSampleRateText(UIConstants::kSampleRate1Hz));
            m_sampleRate->addItem(frameSyncSampleRateText(UIConstants::kSampleRate10Hz));
            m_sampleRate->addItem(frameSyncSampleRateText(UIConstants::kSampleRate100Hz));
            m_sampleRate->setCurrentIndex(cfg.sampleRateIndex);
            addLabeledWidget(hl, "Sample Rate", m_sampleRate);

            hl->addStretch(1);
            layout->addWidget(row);
        }

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
        layout->addWidget(buttons);

        adjustSize();
    }

    QString frameSyncPattern() const { return m_syncPattern->text().trimmed().toUpper(); }
    QString frameSyncMask()    const { return m_syncMask->text().trimmed().toUpper(); }
    int     bitsPerFrame()     const { return m_bitsPerFrame->value(); }
    bool    randomized()       const { return m_randomized->isChecked(); }
    int     sampleRateIndex()  const { return m_sampleRate->currentIndex(); }
    double  dataRateMbps()     const { return m_dataRate->value(); }
    QString lastTomlDir()      const { return m_toml_dir; }

private:
    QLineEdit*      m_syncPattern  = nullptr;
    QLineEdit*      m_syncMask     = nullptr;
    QSpinBox*       m_bitsPerFrame = nullptr;
    QCheckBox*      m_randomized   = nullptr;
    QDoubleSpinBox* m_dataRate     = nullptr;
    QComboBox*      m_sampleRate   = nullptr;
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
        , m_toml_dir(toml_dir)
        , m_receiverParamsToml(cfg.receiverParamsToml)
    {
        setWindowTitle("Receiver SNR Setup — " + cfg.label);
        setModal(true);

        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(14);

        // --- Row 1: Frame Sync, Sync Mask, Bits Per Frame, Load, Save ---
        {
            auto* row = new QWidget(this);
            auto* hl  = new QHBoxLayout(row);
            hl->setContentsMargins(0, 0, 0, 0);
            hl->setSpacing(10);

            m_syncPattern = new QLineEdit(this);
            m_syncPattern->setValidator(new QRegularExpressionValidator(kHexRegex, this));
            m_syncPattern->setText(cfg.frameSyncPattern);
            m_syncPattern->setPlaceholderText("e.g. FE6B2840");
            m_syncPattern->setMinimumWidth(110);
            addLabeledWidget(hl, "Frame Sync", m_syncPattern);

            m_syncMask = new QLineEdit(this);
            m_syncMask->setValidator(new QRegularExpressionValidator(kHexRegex, this));
            m_syncMask->setText(cfg.frameSyncMask);
            m_syncMask->setPlaceholderText("e.g. FFFFFFFF");
            m_syncMask->setMinimumWidth(110);
            addLabeledWidget(hl, "Frame Sync Mask", m_syncMask);

            m_bitsPerFrame = new QSpinBox(this);
            m_bitsPerFrame->setRange(PCMConstants::kMinFrameLengthBits,
                                     PCMConstants::kMaxFrameLengthBits);
            m_bitsPerFrame->setValue(cfg.bitsInMinorFrame);
            m_bitsPerFrame->setMinimumWidth(80);
            addLabeledWidget(hl, "Bits Per Frame", m_bitsPerFrame);

            auto* btnCol = new QVBoxLayout;
            btnCol->setSpacing(2);
            btnCol->addSpacing(18);
            auto* loadBtn = new QPushButton(this);
            loadBtn->setIcon(QIcon(":/resources/folder-open.svg"));
            loadBtn->setToolTip("Load frame sync fields from a TOML file");
            loadBtn->setFixedSize(28, 28);
            auto* saveBtn = new QPushButton(this);
            saveBtn->setIcon(QIcon(":/resources/floppy-save.svg"));
            saveBtn->setToolTip("Save frame sync fields to a TOML file");
            saveBtn->setFixedSize(28, 28);
            btnCol->addWidget(loadBtn);
            btnCol->addWidget(saveBtn);
            btnCol->addStretch();
            hl->addLayout(btnCol);
            hl->addStretch(1);

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

            layout->addWidget(row);
        }

        // --- Row 2: Randomized, Data Rate, Average Period ---
        {
            auto* row = new QWidget(this);
            auto* hl  = new QHBoxLayout(row);
            hl->setContentsMargins(0, 0, 0, 0);
            hl->setSpacing(16);

            m_randomized = new QCheckBox(this);
            m_randomized->setChecked(cfg.randomized);
            {
                auto* col = new QVBoxLayout;
                col->setSpacing(3);
                col->addWidget(new QLabel("Randomized"));
                col->addWidget(m_randomized);
                hl->addLayout(col);
            }

            m_dataRate = new QDoubleSpinBox(this);
            m_dataRate->setRange(0.0, 1000.0);
            m_dataRate->setDecimals(3);
            m_dataRate->setSingleStep(0.1);
            m_dataRate->setSuffix(" Mbps");
            if (cfg.tmatsDataRateMbps > 0.0)
                m_dataRate->setSpecialValueText(
                    QString("Auto (%1 Mbps)").arg(cfg.tmatsDataRateMbps, 0, 'f', 3));
            else
                m_dataRate->setSpecialValueText("Auto (TMATS)");
            m_dataRate->setValue(cfg.dataRateMbps);
            m_dataRate->setMinimumWidth(130);
            addLabeledWidget(hl, "Data Rate (Mbps)", m_dataRate);

            m_sampleRate = new QComboBox(this);
            m_sampleRate->addItem(receiverSNRSampleRateText(UIConstants::kSampleRate1Hz));
            m_sampleRate->addItem(receiverSNRSampleRateText(UIConstants::kSampleRate10Hz));
            m_sampleRate->addItem(receiverSNRSampleRateText(UIConstants::kSampleRate100Hz));
            m_sampleRate->setCurrentIndex(cfg.sampleRateIndex);
            addLabeledWidget(hl, "Average Period", m_sampleRate);

            hl->addStretch(1);
            layout->addWidget(row);
        }

        // --- Separator ---
        {
            auto* sep = new QFrame(this);
            sep->setFrameShape(QFrame::HLine);
            sep->setFrameShadow(QFrame::Sunken);
            layout->addWidget(sep);
        }

        // --- Row 3: Polarity, Slope, Scale (dB/V), Load, Save ---
        {
            auto* row = new QWidget(this);
            auto* hl  = new QHBoxLayout(row);
            hl->setContentsMargins(0, 0, 0, 0);
            hl->setSpacing(10);

            m_polarity = new QComboBox(this);
            m_polarity->addItem("Positive");
            m_polarity->addItem("Negative");
            m_polarity->setCurrentIndex(cfg.polarityIndex);
            addLabeledWidget(hl, "Polarity", m_polarity);

            m_slope = new QComboBox(this);
            m_slope->addItem("±10 V");
            m_slope->addItem("±5 V");
            m_slope->addItem("0–10 V");
            m_slope->addItem("0–5 V");
            m_slope->setCurrentIndex(cfg.slopeIndex);
            addLabeledWidget(hl, "Slope", m_slope);

            m_scale = new QDoubleSpinBox(this);
            m_scale->setRange(0.001, 999.999);
            m_scale->setDecimals(3);
            m_scale->setSuffix(" dB/V");
            m_scale->setValue(cfg.scaleDdBPerV);
            m_scale->setMinimumWidth(100);
            addLabeledWidget(hl, "Scale (dB/V)", m_scale);

            auto* btnCol = new QVBoxLayout;
            btnCol->setSpacing(2);
            btnCol->addSpacing(18);
            auto* loadBtn = new QPushButton(this);
            loadBtn->setIcon(QIcon(":/resources/folder-open.svg"));
            loadBtn->setToolTip("Load receiver parameters from a TOML file");
            loadBtn->setFixedSize(28, 28);
            auto* saveBtn = new QPushButton(this);
            saveBtn->setIcon(QIcon(":/resources/floppy-save.svg"));
            saveBtn->setToolTip("Save receiver parameters to a TOML file");
            saveBtn->setFixedSize(28, 28);
            btnCol->addWidget(loadBtn);
            btnCol->addWidget(saveBtn);
            btnCol->addStretch();
            hl->addLayout(btnCol);
            hl->addStretch(1);

            connect(loadBtn, &QPushButton::clicked, this, [this]() {
                QString filename = QFileDialog::getOpenFileName(
                    this, tr("Load Receiver Parameters"), m_toml_dir,
                    tr("TOML Files (*.toml);;All Files (*.*)"));
                if (filename.isEmpty()) return;
                m_toml_dir = QFileInfo(filename).absolutePath();
                m_receiverParamsToml = filename;
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
            connect(saveBtn, &QPushButton::clicked, this, [this]() {
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

            layout->addWidget(row);
        }

        // --- Row 4: Num Receivers, Receiver Channels ---
        {
            auto* row = new QWidget(this);
            auto* hl  = new QHBoxLayout(row);
            hl->setContentsMargins(0, 0, 0, 0);
            hl->setSpacing(16);

            m_numReceivers = new QSpinBox(this);
            m_numReceivers->setRange(1, 100);
            m_numReceivers->setValue(cfg.numReceivers);
            addLabeledWidget(hl, "Num Receivers", m_numReceivers);

            m_receiverChannels = new QSpinBox(this);
            m_receiverChannels->setRange(1, 100);
            m_receiverChannels->setValue(cfg.receiverChannels);
            addLabeledWidget(hl, "Receiver Channels", m_receiverChannels);

            hl->addStretch(1);
            layout->addWidget(row);
        }

        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
            if (m_syncPattern->text().trimmed().isEmpty())
            {
                QMessageBox::warning(this, tr("Missing Frame Sync"),
                    tr("A frame sync pattern is required (e.g. FE6B2840)."));
                return;
            }
            if (m_receiverParamsToml.isEmpty())
            {
                QMessageBox::warning(this, tr("Missing Receiver Parameters"),
                    tr("A Receiver Parameters TOML file must be loaded (use the Load button "
                       "in the receiver section)."));
                return;
            }
            accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);

        adjustSize();
    }

    QString frameSyncPattern()   const { return m_syncPattern->text().trimmed().toUpper(); }
    QString frameSyncMask()      const { return m_syncMask->text().trimmed().toUpper(); }
    int     bitsPerFrame()       const { return m_bitsPerFrame->value(); }
    bool    randomized()         const { return m_randomized->isChecked(); }
    int     sampleRateIndex()    const { return m_sampleRate->currentIndex(); }
    double  dataRateMbps()       const { return m_dataRate->value(); }
    int     polarityIndex()      const { return m_polarity->currentIndex(); }
    int     slopeIndex()         const { return m_slope->currentIndex(); }
    double  scaleDdBPerV()       const { return m_scale->value(); }
    int     numReceivers()       const { return m_numReceivers->value(); }
    int     receiverChannels()   const { return m_receiverChannels->value(); }
    QString receiverParamsToml() const { return m_receiverParamsToml; }
    QString lastTomlDir()        const { return m_toml_dir; }

private:
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
    resize(700, 500);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(10);

    // Time channel row
    {
        auto* row = new QWidget(this);
        auto* hl  = new QHBoxLayout(row);
        hl->setContentsMargins(0, 4, 0, 4);
        hl->addWidget(new QLabel("Time Channel:"));
        m_time_channel_combo = new QComboBox(row);
        m_time_channel_combo->addItems(time_channels);
        m_time_channel_combo->setEnabled(!time_channels.isEmpty());
        if (!time_channels.isEmpty())
        {
            int idx = (time_channel_index > 0) ? time_channel_index - 1 : 0;
            m_time_channel_combo->setCurrentIndex(idx);
        }
        hl->addWidget(m_time_channel_combo);
        hl->addStretch(1);
        layout->addWidget(row);
    }

    m_table = new QTableWidget(this);
    m_table->setStyleSheet("QTableWidget { gridline-color: palette(mid); }"
                           "QTableWidget::item { padding: 4px; }");
    layout->addWidget(m_table, 1);

    buildTable();

    // Separator above time controls
    auto* hsep = new QFrame(this);
    hsep->setFrameShape(QFrame::HLine);
    hsep->setFrameShadow(QFrame::Sunken);
    layout->addWidget(hsep);

    auto* time_label = new QLabel("Time Controls", this);
    QFont bold_font = time_label->font();
    bold_font.setBold(true);
    time_label->setFont(bold_font);
    layout->addWidget(time_label);

    m_time_widget = new TimeExtractionWidget(this);
    m_time_widget->setExtractAllTime(extract_all_time);
    m_time_widget->fillTimes(start_time, stop_time);
    m_time_widget->setAllEnabled(true);
    layout->addWidget(m_time_widget);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &StreamConfigDialog::validateAndAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void StreamConfigDialog::buildTable()
{
    constexpr int kColProcess = 0;
    constexpr int kColChannel = 1;
    constexpr int kColMode    = 2;
    constexpr int kColSetup   = 3;
    constexpr int kColReady   = 4;
    constexpr int kRowHeight  = 36;

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
        auto* l = new QHBoxLayout(container);
        l->setContentsMargins(4, 2, 4, 2);
        l->setAlignment(Qt::AlignCenter);
        l->addWidget(inner);
        return container;
    };

    auto paddedWidget = [](QWidget* inner) -> QWidget* {
        auto* container = new QWidget;
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
        w.sampleRateIndex   = cfg.sampleRateIndex;
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
        w.gearBtn->setFixedSize(28, 28);
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

    if (!checked)
    {
        w.readyLabel->setText("<span style='color: gray; font-size: 16px;'>✗</span>");
        return;
    }

    if (!w.gearConfirmed)
    {
        w.readyLabel->setText("<span style='color: red; font-size: 16px;'>✗</span>");
        return;
    }

    bool frame_ok = !w.frameSyncPattern.isEmpty();
    bool recv_ok  = (w.mode->currentIndex() == 1) || !w.receiverParamsToml.isEmpty();

    if (frame_ok && recv_ok)
        w.readyLabel->setText("<span style='color: green; font-size: 16px;'>✓</span>");
    else
        w.readyLabel->setText("<span style='color: red; font-size: 16px;'>✗</span>");
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
    temp.sampleRateIndex    = w.sampleRateIndex;
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
            w.sampleRateIndex    = dlg.sampleRateIndex();
            w.dataRateMbps       = dlg.dataRateMbps();
            m_toml_dir           = dlg.lastTomlDir();
            w.lastConfiguredMode = StreamMode::FrameSyncLockStats;
            w.gearConfirmed      = true;
            updateReadyIcon(row);
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
            w.sampleRateIndex    = dlg.sampleRateIndex();
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
        }
    }
}

void StreamConfigDialog::validateAndAccept()
{
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

        if (w.mode->currentIndex() == 0 && w.receiverParamsToml.isEmpty())
        {
            QMessageBox::warning(this, tr("Missing Receiver Parameters"),
                channel + tr(": a Receiver Parameters TOML file is required for "
                             "Receiver SNR mode. Click the gear icon to configure this stream."));
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
        result[row].sampleRateIndex   = w.sampleRateIndex;
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
