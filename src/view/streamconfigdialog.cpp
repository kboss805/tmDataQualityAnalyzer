/**
 * @file streamconfigdialog.cpp
 * @brief Implementation of StreamConfigDialog and its per-mode sub-dialogs.
 */

#include "streamconfigdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
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
// Reuses the sub-dialogs' "small gap between related controls" unit so the two
// boxed cells (Channel, Mode) read as adjacent-but-distinct instead of merging
// into one continuous box. Applied identically to the header and data rows so
// the columns stay aligned.
constexpr int kColumnGap       = DialogLayout::kControlGap;
// The Channel cell now has a combo-box-style border + inset padding (see the
// channelNameCell QSS rule), so its available text width is narrower than the
// column: border (~2px) + the QSS's own left/right padding (8px each). A little
// slack beyond that exact sum is harmless — elidedText() is a max-width bound,
// never an exact fit, so erring larger just shows a couple fewer characters.
constexpr int kCellTextPadding = 20; ///< Safety margin subtracted before eliding cell text.
constexpr int kReadyGlyphPt    = 28; ///< Font size of the Ready-column ✓/✗ glyph.
// Matches the theme QSS's "QComboBox { height: 22px; }" plus its 1px border on
// each side, so the boxed Channel cell is exactly as tall as the Mode combo
// it's meant to visually pair with, rather than stretching to the full row.
constexpr int kFieldCellHeight = 24;

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
                                       bool swap_bytes,
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

        // Byte order sits on the Time Channel row because it shares that row's scope:
        // both describe the FILE, not a stream. Every per-stream control lives in the
        // table below or behind a row's gear. Putting this in the gear dialog would
        // imply channels of one recording can disagree about their own byte order.
        m_swap_bytes = new QCheckBox("Swap byte pairs", this);
        m_swap_bytes->setChecked(swap_bytes);
        m_swap_bytes->setToolTip(
            "Swap each pair of bytes in the recorded PCM data before searching for the\n"
            "frame sync pattern. Byte order depends on the recorder that produced the\n"
            "file, so it applies to every stream in this recording.\n\n"
            "Leave this on unless no stream finds sync: that is the symptom of the\n"
            "wrong byte order, and the log says so when it detects it.");
        hl->addWidget(m_swap_bytes);
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
        hl->addSpacing(kColumnGap);
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
    // primary + Cancel. The "All" checkbox carries its own text (no extra label).
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

        // Channel label: left-justified, matching Mode and the rest of the dialog's
        // inputs. Elision follows the alignment — ElideRight, so the text always runs
        // from the cell's left edge and any "…" sits at the end where it is expected.
        //
        // This trades away something real: these names share a long common prefix
        // ("CH-01 2250.5MHZ AGC …"), and the DISTINGUISHING detail (band, rate, code)
        // is at the end, so a truncated long name now hides the part that tells two
        // channels apart. Accepted deliberately — most names fit, right-aligned text
        // read oddly for those, and the full name is always in the tooltip.
        // Styled (via the channelNameCell object name) to mimic the Mode combo
        // box's border/fill, so the two columns read as a matched pair.
        {
            auto* lbl = new QLabel(rowWidget);
            lbl->setObjectName("channelNameCell");
            lbl->setFixedWidth(kColWidthChannel);
            lbl->setFixedHeight(kFieldCellHeight);
            lbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            lbl->setText(QFontMetrics(lbl->font()).elidedText(
                cfg.label, Qt::ElideRight, kColWidthChannel - kCellTextPadding));
            lbl->setToolTip(cfg.label);
            lbl->setAutoFillBackground(false);
            // Read-only: the label just mimics a combo box's look for visual
            // pairing with Mode. Disabled so it reads that way too (the QSS
            // ":disabled" rule matches every other input control's dimmed
            // treatment) instead of looking editable.
            lbl->setEnabled(false);
            hl->addWidget(lbl, 0, Qt::AlignVCenter);
            hl->addSpacing(kColumnGap);
        }

        // Mode combo. Made editable-but-readonly so the CLOSED box's current-value
        // text can be right-justified — QComboBox has no built-in alignment option
        // for it — keeping "Frame Sync Lock"/"Receiver SNR" from crowding against
        // the Channel column's now also-right-justified text. Selecting from the
        // dropdown list (opened via the arrow) is unaffected; typing is disabled.
        w.mode = new QComboBox(rowWidget);
        w.mode->addItem("Receiver SNR");
        w.mode->addItem("Frame Sync Lock");
        w.mode->setCurrentIndex(cfg.mode == StreamMode::FrameSyncLockStats ? 1 : 0);
        w.mode->setFixedWidth(kColWidthMode);
        w.mode->setToolTip("Analysis mode: Receiver SNR measures channel signal quality; Frame Sync Lock measures synchronization stability.");
        w.mode->setEditable(true);
        w.mode->lineEdit()->setReadOnly(true);
        // Left-aligned, like every other control in the dialog. It was right-aligned
        // to keep it from crowding the Channel column, but that column is fixed-width
        // and separated by the grid, so nothing was actually crowded - the text just
        // sat oddly far from the label it belongs to.
        w.mode->lineEdit()->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        w.mode->lineEdit()->setCursor(Qt::ArrowCursor); // reads as a selector, not free text
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
        w.readyLabel->setText(readyMarkup(UIConstants::kStatusIdleColor, "✗"));
        w.readyLabel->setToolTip(QString());
    }
    else if (!w.gearConfirmed)
    {
        w.readyLabel->setText(readyMarkup(UIConstants::kStatusFailColor, "✗"));
        w.readyLabel->setToolTip("Click the gear icon to configure this stream.");
    }
    else if (!w.frameSyncPattern.isEmpty())
    {
        w.readyLabel->setText(readyMarkup(UIConstants::kStatusOkColor, "✓"));
        w.readyLabel->setToolTip(QString());
    }
    else
    {
        w.readyLabel->setText(readyMarkup(UIConstants::kStatusFailColor, "✗"));
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
    temp.calCh10Path        = w.calCh10Path;
    temp.stepTomlPath       = w.stepTomlPath;
    temp.clipStartSec       = w.clipStartSec;
    temp.clipEndSec         = w.clipEndSec;

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
                rw.calCh10Path        = dlg.calCh10Path();
                rw.stepTomlPath       = dlg.stepTomlPath();
                rw.clipStartSec       = dlg.clipStartSec();
                rw.clipEndSec         = dlg.clipEndSec();
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

bool StreamConfigDialog::swapBytes() const
{
    return m_swap_bytes != nullptr && m_swap_bytes->isChecked();
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
        result[row].calCh10Path       = w.calCh10Path;
        result[row].stepTomlPath      = w.stepTomlPath;
        result[row].clipStartSec      = w.clipStartSec;
        result[row].clipEndSec        = w.clipEndSec;
    }
    return result;
}
