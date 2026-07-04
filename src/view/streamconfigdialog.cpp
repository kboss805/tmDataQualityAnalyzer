/**
 * @file streamconfigdialog.cpp
 * @brief Implementation of StreamConfigDialog and its per-mode sub-dialogs.
 */

#include "streamconfigdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QSpinBox>
#include <QScrollArea>
#include <QVBoxLayout>

#include "calibrationextractor.h"
#include "constants.h"
#include "stepdetector.h"
#include "tomlconfighelper.h"

#include "streamsubdialogs.h"

namespace {

// Stream-table geometry, shared by the header row, the data rows, and the
// scroll-area height cap so the columns line up and stay in sync.
constexpr int kColWidthProcess = 48;
constexpr int kColWidthChannel = 175; ///< Channel names past this width elide with "...".
constexpr int kColWidthMode    = 175;
constexpr int kColWidthSetup   = 64;
constexpr int kColWidthReady   = 64;
constexpr int kRowHeight       = 52;
constexpr int kCellTextPadding = 8; ///< Safety margin subtracted before eliding cell text.
constexpr int kReadyGlyphPt    = 28; ///< Font size of the Ready-column ✓/✗ glyph.

/// Builds the Ready-column status glyph markup (a large colored ✓ or ✗).
QString readyMarkup(const char* color, const QString& glyph)
{
    return QString("<span style='color: %1; font-size: %2px;'>%3</span>")
        .arg(color).arg(kReadyGlyphPt).arg(glyph);
}

} // namespace

////////////////////////////////////////////////////////////////////////////////
//                         STREAM CONFIG DIALOG                               //
////////////////////////////////////////////////////////////////////////////////

StreamConfigDialog::StreamConfigDialog(const QVector<StreamConfig>& configs,
                                       const QString& toml_dir,
                                       const QStringList& time_channels,
                                       int time_channel_index,
                                       int time_channel_id,
                                       const QString& app_root,
                                       QWidget* parent)
    : QDialog(parent)
    , m_configs(configs)
    , m_toml_dir(toml_dir)
    , m_app_root(app_root)
    , m_time_channel_id(time_channel_id)
{
    setWindowTitle("Configure Streams");
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(8);

    // Time channel row
    {
        auto* hl = new QHBoxLayout;
        hl->setContentsMargins(0, 4, 0, 4);
        hl->addWidget(new QLabel("Time Channel:"));
        m_time_channel_combo = new QComboBox(this);
        m_time_channel_combo->addItems(time_channels);
        m_time_channel_combo->setEnabled(!time_channels.isEmpty());
        m_time_channel_combo->setToolTip("IRIG time channel used to timestamp frame samples. Required for calibration extraction.");
        if (!time_channels.isEmpty())
        {
            int idx = (time_channel_index > 0) ? time_channel_index - 1 : 0;
            m_time_channel_combo->setCurrentIndex(idx);
        }
        hl->addWidget(m_time_channel_combo);
        hl->addStretch(1);
        layout->addLayout(hl);
    }

    layout->addSpacing(8);

    // Column header row
    {
        auto* header = new QWidget(this);
        auto* hl = new QHBoxLayout(header);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(0);

        // Dimmed header text; color comes from the theme QSS
        // (QLabel#streamHeaderLabel) so it stays legible in both light and dark
        // rather than the old hard-coded white that vanished on the light theme.
        auto makeHdr = [&](const QString& text, int width) {
            auto* lbl = new QLabel(text, header);
            lbl->setObjectName("streamHeaderLabel");
            lbl->setFixedWidth(width);
            lbl->setAlignment(Qt::AlignCenter | Qt::AlignVCenter);
            hl->addWidget(lbl);
        };
        makeHdr("Process",   kColWidthProcess);
        makeHdr("Channel",   kColWidthChannel);
        makeHdr("Mode",      kColWidthMode);
        makeHdr("Configure", kColWidthSetup);
        makeHdr("Ready",     kColWidthReady);
        hl->addStretch(1);

        layout->addWidget(header);

        // Separator line beneath headers; color from the theme QSS
        // (QFrame#streamHeaderSeparator) for the same light/dark reason as above.
        auto* line = new QFrame(this);
        line->setObjectName("streamHeaderSeparator");
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Plain);
        layout->addWidget(line);
    }

    // Scrollable stream rows
    m_stream_container = new QWidget(this);
    m_stream_container->setAutoFillBackground(false);

    auto* containerLayout = new QVBoxLayout(m_stream_container);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(0);

    m_scroll_area = new QScrollArea(this);
    m_scroll_area->setWidget(m_stream_container);
    m_scroll_area->setWidgetResizable(true);
    m_scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll_area->setFrameShape(QFrame::NoFrame);
    m_scroll_area->setStyleSheet("QScrollArea { background: transparent; border: none; }");
    m_scroll_area->viewport()->setAutoFillBackground(false);

    buildTable();

    // Cap the scroll area to 8 visible rows; shrink if fewer streams exist.
    constexpr int kVisibleRows  = 8;
    const int visible = qMin(static_cast<int>(m_configs.size()), kVisibleRows);
    m_scroll_area->setFixedHeight(visible * kRowHeight);

    layout->addWidget(m_scroll_area);

    DialogButtons btns = makeDialogButtons(this, tr("Process"));
    m_ok_btn = btns.primary;
    connect(m_ok_btn, &QPushButton::clicked, this, &StreamConfigDialog::validateAndAccept);

    m_all_toggle = new QCheckBox(tr("All"), this);
    m_all_toggle->setToolTip("Toggle all streams on or off.");
    connect(m_all_toggle, &QCheckBox::toggled, this, [this](bool checked) {
        for (RowWidgets& w : m_rows)
            w.process->setChecked(checked);
    });

    // Same footer builder as the sub-dialogs: bottom-left toggle, stretch, then
    // Cancel + primary. The "All" checkbox carries its own text (no extra label).
    addBottomBar(layout, btns, this, m_all_toggle);

    // m_ok_btn was nullptr during buildTable(), so call once now to reflect actual state.
    updateOkButton();
    adjustSize();
}

void StreamConfigDialog::buildTable()
{
    auto* containerLayout = qobject_cast<QVBoxLayout*>(m_stream_container->layout());

    m_rows.resize(static_cast<int>(m_configs.size()));

    for (int row = 0; row < m_configs.size(); row++)
    {
        const StreamConfig& cfg = m_configs[row];
        RowWidgets& w = m_rows[row];

        // Seed stored values from the incoming config
        w.frameSyncPattern   = cfg.sync.pattern;
        w.frameSyncMask      = cfg.sync.mask;
        w.bitsInFrame        = cfg.sync.bitsInMinorFrame;
        w.randomized         = cfg.sync.randomized;
        w.inverted           = cfg.sync.inverted;
        w.samplePeriodIndex  = cfg.samplePeriodIndex;
        w.dataRateMbps       = cfg.sync.dataRateMbps;
        w.polarityIndex      = cfg.polarityIndex;
        w.slopeIndex         = cfg.slopeIndex;
        w.scaleDdBPerV       = cfg.scaleDdBPerV;
        w.numReceivers       = cfg.numReceivers;
        w.receiverChannels   = cfg.receiverChannels;
        w.receiverParamsToml = cfg.receiverParamsToml;
        w.lastConfiguredMode = cfg.mode;

        // Row container
        auto* rowWidget = new QWidget(m_stream_container);
        rowWidget->setFixedHeight(kRowHeight);
        rowWidget->setAutoFillBackground(false);

        auto* hl = new QHBoxLayout(rowWidget);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(0);

        // Process toggle (centered in fixed-width cell, matching the header)
        w.process = new QCheckBox(rowWidget);
        w.process->setChecked(cfg.process);
        {
            auto* cell = new QWidget(rowWidget);
            cell->setFixedWidth(kColWidthProcess);
            cell->setAutoFillBackground(false);
            auto* cl = new QHBoxLayout(cell);
            cl->setContentsMargins(0, 0, 0, 0);
            cl->setAlignment(Qt::AlignCenter);
            cl->addWidget(w.process);
            hl->addWidget(cell);
        }

        // Channel label: right-justified and elided on the LEFT ("…RNRZ-L") so the
        // END of a long TMATS-derived name — where the distinguishing detail (band,
        // rate, code) usually sits — stays visible instead of the common prefix. The
        // cell doesn't move; only the text alignment. Full name is in the tooltip.
        {
            auto* lbl = new QLabel(rowWidget);
            lbl->setFixedWidth(kColWidthChannel);
            lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            lbl->setText(QFontMetrics(lbl->font()).elidedText(
                cfg.label, Qt::ElideLeft, kColWidthChannel - kCellTextPadding));
            lbl->setToolTip(cfg.label);
            lbl->setAutoFillBackground(false);
            hl->addWidget(lbl);
        }

        // Mode combo
        w.mode = new QComboBox(rowWidget);
        w.mode->addItem("Receiver SNR");
        w.mode->addItem("Frame Sync Lock");
        w.mode->setCurrentIndex(cfg.mode == StreamMode::FrameSyncLockStats ? 1 : 0);
        w.mode->setFixedWidth(kColWidthMode);
        w.mode->setToolTip("Analysis mode: Receiver SNR measures channel signal quality; Frame Sync Lock measures synchronization stability.");
        hl->addWidget(w.mode);

        // Gear button (centered in fixed-width cell)
        w.gearBtn = new QPushButton(rowWidget);
        w.gearBtn->setIcon(QIcon(":/resources/gear.svg"));
        w.gearBtn->setToolTip("Open configuration dialog for this stream's frame sync, data rate, and receiver parameters.");
        styleIconButton(w.gearBtn, DialogLayout::kIconButtonSize);
        w.gearBtn->setEnabled(cfg.process);
        {
            auto* cell = new QWidget(rowWidget);
            cell->setFixedWidth(kColWidthSetup);
            cell->setAutoFillBackground(false);
            auto* cl = new QHBoxLayout(cell);
            cl->setContentsMargins(0, 0, 0, 0);
            cl->setAlignment(Qt::AlignCenter);
            cl->addWidget(w.gearBtn);
            hl->addWidget(cell);
        }

        // Ready label (centered in fixed-width cell)
        w.readyLabel = new QLabel(rowWidget);
        w.readyLabel->setTextFormat(Qt::RichText);
        w.readyLabel->setAlignment(Qt::AlignCenter);
        w.readyLabel->setAutoFillBackground(false);
        {
            auto* cell = new QWidget(rowWidget);
            cell->setFixedWidth(kColWidthReady);
            cell->setAutoFillBackground(false);
            auto* cl = new QHBoxLayout(cell);
            cl->setContentsMargins(0, 0, 0, 0);
            cl->setAlignment(Qt::AlignCenter);
            cl->addWidget(w.readyLabel);
            hl->addWidget(cell);
        }

        hl->addStretch(1);
        containerLayout->addWidget(rowWidget);

        updateReadyIcon(row);

        connect(w.process, &QCheckBox::toggled, this, [this, row](bool) {
            updateReadyIcon(row);
        });
        connect(w.mode, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, row](int) { updateReadyIcon(row); });
        connect(w.gearBtn, &QPushButton::clicked,
                this, [this, row]() { openGearDialog(row); });
    }

    containerLayout->addStretch(1);
}

void StreamConfigDialog::updateReadyIcon(int row)
{
    RowWidgets& w = m_rows[row];
    bool checked = w.process->isChecked();
    w.gearBtn->setEnabled(checked);
    w.mode->setEnabled(checked);

    if (!checked)
    {
        w.readyLabel->setText(readyMarkup("gray", "✗"));
        w.readyLabel->setToolTip(QString());
    }
    else if (!w.gearConfirmed)
    {
        w.readyLabel->setText(readyMarkup("red", "✗"));
        w.readyLabel->setToolTip("Click the gear icon to configure this stream.");
    }
    else if (!w.frameSyncPattern.isEmpty())
    {
        w.readyLabel->setText(readyMarkup("green", "✓"));
        w.readyLabel->setToolTip(QString());
    }
    else
    {
        w.readyLabel->setText(readyMarkup("red", "✗"));
        w.readyLabel->setToolTip("A frame sync pattern is required.");
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
    temp.sync.pattern   = w.frameSyncPattern;
    temp.sync.mask      = w.frameSyncMask;
    temp.sync.bitsInMinorFrame   = w.bitsInFrame;
    temp.sync.randomized         = w.randomized;
    temp.sync.inverted           = w.inverted;
    temp.samplePeriodIndex    = w.samplePeriodIndex;
    temp.sync.dataRateMbps       = w.dataRateMbps;
    temp.polarityIndex      = w.polarityIndex;
    temp.slopeIndex         = w.slopeIndex;
    temp.scaleDdBPerV       = w.scaleDdBPerV;
    temp.numReceivers       = w.numReceivers;
    temp.receiverChannels   = w.receiverChannels;
    temp.receiverParamsToml = w.receiverParamsToml;
    temp.calibrationByWord  = w.calibrationByWord;

    // If the user changed modes since last configure, reset frame sync fields to
    // this mode's defaults so the sub-dialog pre-fills with sensible values.
    if (current_mode != w.lastConfiguredMode)
    {
        if (is_frame_sync_lock)
        {
            temp.sync.mask     = PCMConstants::kDefaultFrameSyncMask;
            temp.sync.pattern  = PCMConstants::kDefaultFrameSyncLockPattern;
            temp.sync.bitsInMinorFrame  = PCMConstants::kDefaultFrameSyncLockBits;
            temp.receiverParamsToml.clear();
        }
        else
        {
            // Receiver SNR defaults are loaded from framesync_rcvr_default.toml (not
            // hard-coded) so the user can change the receiver frame sync.
            const ReceiverFrameDefaults d = loadReceiverFrameDefaults(m_app_root);
            temp.sync.pattern = d.pattern;
            temp.sync.mask    = d.mask;
            temp.sync.bitsInMinorFrame = d.bits;
        }
    }

    if (is_frame_sync_lock)
    {
        FrameLockSetupDialog dlg(temp, m_toml_dir, m_app_root, this);
        if (dlg.exec() == QDialog::Accepted)
        {
            // Single copy of the dialog->row field mapping, applied to the edited
            // row and (on Apply to All) to every other same-mode row, so the two
            // can't drift out of sync.
            auto applyLock = [&dlg](RowWidgets& rw) {
                rw.frameSyncPattern   = dlg.frameSyncPattern();
                rw.frameSyncMask      = dlg.frameSyncMask();
                rw.bitsInFrame        = dlg.bitsPerFrame();
                rw.randomized         = dlg.randomized();
                rw.inverted           = dlg.inverted();
                rw.samplePeriodIndex  = dlg.samplePeriodIndex();
                rw.dataRateMbps       = dlg.dataRateMbps();
                rw.lastConfiguredMode = StreamMode::FrameSyncLockStats;
                rw.gearConfirmed      = true;
            };

            applyLock(w);
            m_toml_dir = dlg.lastTomlDir();
            updateReadyIcon(row);

            if (dlg.applyToAll())
            {
                for (int i = 0; i < m_rows.size(); i++)
                {
                    if (i == row || !m_rows[i].process->isChecked()) continue;
                    if (m_rows[i].mode->currentIndex() != 1) continue; // different mode: leave unchanged
                    applyLock(m_rows[i]);
                    updateReadyIcon(i);
                }
            }
        }
    }
    else
    {
        ReceiverSNRDialog dlg(temp, m_toml_dir, m_time_channel_id, m_app_root, this);
        if (dlg.exec() == QDialog::Accepted)
        {
            // Single copy of the dialog->row field mapping (see the lock branch).
            auto applySnr = [&dlg](RowWidgets& rw) {
                rw.frameSyncPattern   = dlg.frameSyncPattern();
                rw.frameSyncMask      = dlg.frameSyncMask();
                rw.bitsInFrame        = dlg.bitsPerFrame();
                rw.randomized         = dlg.randomized();
                rw.inverted           = dlg.inverted();
                rw.samplePeriodIndex  = dlg.samplePeriodIndex();
                rw.dataRateMbps       = dlg.dataRateMbps();
                rw.polarityIndex      = dlg.polarityIndex();
                rw.slopeIndex         = dlg.slopeIndex();
                rw.scaleDdBPerV       = dlg.scaleDdBPerV();
                rw.numReceivers       = dlg.numReceivers();
                rw.receiverChannels   = dlg.receiverChannels();
                rw.receiverParamsToml = dlg.receiverParamsToml();
                rw.calibrationByWord  = dlg.calibrationByWord();
                rw.lastConfiguredMode = StreamMode::ReceiverChannelInfo;
                rw.gearConfirmed      = true;
            };

            applySnr(w);
            m_toml_dir = dlg.lastTomlDir();
            updateReadyIcon(row);

            if (dlg.applyToAll())
            {
                for (int i = 0; i < m_rows.size(); i++)
                {
                    if (i == row || !m_rows[i].process->isChecked()) continue;
                    if (m_rows[i].mode->currentIndex() != 0) continue; // different mode: leave unchanged
                    applySnr(m_rows[i]);
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

QVector<StreamConfig> StreamConfigDialog::configs() const
{
    QVector<StreamConfig> result = m_configs;
    for (int row = 0; row < m_rows.size() && row < result.size(); row++)
    {
        const RowWidgets& w = m_rows[row];
        result[row].process           = w.process->isChecked();
        result[row].mode              = (w.mode->currentIndex() == 1)
            ? StreamMode::FrameSyncLockStats : StreamMode::ReceiverChannelInfo;
        result[row].sync.pattern  = w.frameSyncPattern;
        result[row].sync.mask     = w.frameSyncMask;
        result[row].sync.bitsInMinorFrame  = w.bitsInFrame;
        result[row].sync.randomized        = w.randomized;
        result[row].sync.inverted          = w.inverted;
        result[row].samplePeriodIndex   = w.samplePeriodIndex;
        result[row].sync.dataRateMbps      = w.dataRateMbps;
        result[row].polarityIndex     = w.polarityIndex;
        result[row].slopeIndex        = w.slopeIndex;
        result[row].scaleDdBPerV      = w.scaleDdBPerV;
        result[row].numReceivers      = w.numReceivers;
        result[row].receiverChannels  = w.receiverChannels;
        result[row].receiverParamsToml = w.receiverParamsToml;
        result[row].calibrationByWord = w.calibrationByWord;
    }
    return result;
}
