/**
 * @file streamsubdialogs.h
 * @brief Per-mode sub-dialogs for StreamConfigDialog (Frame Lock Setup,
 *        Calibration Setup, Receiver SNR) and their shared helpers.
 *
 * Private implementation header: included ONLY by streamconfigdialog.cpp. The
 * anonymous namespace keeps these symbols internal to that single translation
 * unit, exactly as when they lived inline in the .cpp.
 */

#ifndef STREAMSUBDIALOGS_H
#define STREAMSUBDIALOGS_H

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

#include "calibrationextractor.h"
#include "constants.h"
#include "framesetup.h"
#include "framesyncparams.h"
#include "stepdetector.h"
#include "streamconfig.h"
#include "tomlconfighelper.h"

namespace {

/// Shared layout metrics so every stream dialog uses the same vertical rhythm and
/// grid geometry. Previously each dialog hand-picked these (outer spacing varied
/// 4/6/8 across the three; the input→Load/Save gap column and button offsets were
/// re-declared per call site), which is what let the look-and-feel drift.
namespace DialogLayout {
    constexpr int kOuterSpacing     = 6;   ///< Spacing between a dialog's top-level rows.
    constexpr int kSectionGap       = 16;  ///< Larger gap that introduces a logical group.
    constexpr int kControlGap       = 8;   ///< Small gap between closely related controls.
    constexpr int kGridHSpacing     = 10;  ///< Horizontal spacing inside a form grid.
    constexpr int kGridVSpacing     = 4;   ///< Vertical spacing inside a form grid.
    constexpr int kColGapWidth      = 16;  ///< Width of the gap column before Load/Save buttons.
    constexpr int kIconButtonSize   = 28;  ///< Standard icon-button edge (gear, load, save).
    constexpr int kCheckboxLabelGap = 6;   ///< Gap between a checkbox and its text label.
}

/// Validator for the frame sync pattern / mask fields: hex characters only, no
/// longer than the documented maximum sync length (US2.0/US7.0).
///
/// Built from the constants rather than spelled out, so the field can't drift
/// from them: the character class comes from PCMConstants::kFrameSyncHexPattern
/// ("^[0-9A-Fa-f]+$") with its "+" quantifier swapped for the bound derived from
/// kMaxSyncPatternBits. Raising the bit limit widens this field automatically.
inline QRegularExpression makeSyncPatternValidatorRegex()
{
    const QString bounded =
        QString::fromLatin1(PCMConstants::kFrameSyncHexPattern)
            .replace(QStringLiteral("+$"),
                     QStringLiteral("{1,%1}$").arg(PCMConstants::kMaxSyncPatternHexChars));
    return QRegularExpression(bounded);
}

const QRegularExpression kHexRegex = makeSyncPatternValidatorRegex();

QString periodText(double sec)
{
    if (sec >= 1.0)
        return QString::number(static_cast<int>(sec)) + " s";
    return QString::number(static_cast<int>(sec * 1000)) + " ms";
}

/// Resolves the settings subdirectory named @p subdir under @p app_root's
/// settings directory, for use as a file dialog's starting directory. Falls
/// back to @p fallback_dir if app_root is empty or the subdirectory doesn't
/// exist (e.g. a dev build run without the settings/ tree alongside it).
QString settingsSubdir(const QString& app_root, const char* subdir, const QString& fallback_dir)
{
    if (app_root.isEmpty())
        return fallback_dir;

    QString dir = app_root + "/" + UIConstants::kSettingsDirName + "/" + subdir;
    return QDir(dir).exists() ? dir : fallback_dir;
}

/// Stylesheet for the primary (OK / Process) action button — WinUI 3 accent blue.
const char* kBlueButtonStyle =
    "QPushButton {"
    "  background-color: #0067C0; color: white;"
    "  border: none; border-radius: 4px;"
    "  padding: 4px 16px; min-width: 64px;"
    "}"
    "QPushButton:hover   { background-color: #1A74C7; }"
    "QPushButton:pressed { background-color: #0055A8; }"
    "QPushButton:disabled{ background-color: #88B0D4; }";

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
                           QString&   toml_dir,
                           QCheckBox* invertedBox = nullptr)
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

    if (invertedBox)
    {
        bool inv = cfg.value("Frame/Inverted", false).toBool();
        invertedBox->setChecked(inv);
    }
}

/// Saves frame sync fields to a TOML file.
void saveFrameSyncToToml(const QString& filename,
                         const QString& syncPattern,
                         const QString& syncMask,
                         int            bitsPerFrame,
                         bool           inverted,
                         QString&       toml_dir)
{
    toml_dir = QFileInfo(filename).absolutePath();
    QSettings cfg(filename, TomlConfigHelper::format());
    cfg.beginGroup("Frame");
    cfg.setValue("FrameSync",     syncPattern);
    cfg.setValue("FrameSyncMask", syncMask);
    cfg.setValue("BitsPerFrame",  bitsPerFrame);
    cfg.setValue("Inverted",      inverted);
    cfg.endGroup();
    cfg.sync();
}

/// Default Receiver SNR frame parameters (pattern, mask, bits-per-frame),
/// loaded from settings/framesync_patterns/framesync_rcvr_default.toml so they are
/// user-editable rather than hard-coded. Falls back to the compiled constants
/// if the file is missing or a field is absent/invalid.
struct ReceiverFrameDefaults
{
    QString pattern = PCMConstants::kDefaultReceiverSNRPattern;
    QString mask    = PCMConstants::kDefaultFrameSyncMask;
    int     bits    = PCMConstants::kDefaultReceiverSNRBits;
};

ReceiverFrameDefaults loadReceiverFrameDefaults(const QString& app_root)
{
    ReceiverFrameDefaults d;
    const QString path = app_root + "/" + UIConstants::kSettingsDirName + "/" +
        UIConstants::kFramesyncPatternsDirName + "/" +
        UIConstants::kDefaultReceiverFrameSyncFilename;
    if (!QFileInfo::exists(path))
        return d;

    QSettings cfg(path, TomlConfigHelper::format());
    const QString sync = cfg.value("Frame/FrameSync").toString();
    if (!sync.isEmpty())
        d.pattern = sync.toUpper();
    const QString mask = cfg.value("Frame/FrameSyncMask").toString();
    if (!mask.isEmpty())
        d.mask = mask.toUpper();
    const int bits = cfg.value("Frame/BitsPerFrame", 0).toInt();
    if (bits >= PCMConstants::kMinFrameLengthBits)
        d.bits = bits;
    return d;
}

/// Makes @p control the same height as @p reference, so combo boxes line up with
/// the line edits sharing their row.
void matchControlHeight(QWidget* control, const QWidget* reference)
{
    control->setFixedHeight(reference->sizeHint().height());
}

/// Bundles the frame-sync input widgets shared by the Frame Sync Lock and
/// Receiver SNR dialogs so a single builder can create them.
struct FrameSyncWidgets
{
    QLineEdit*      syncPattern  = nullptr;
    QLineEdit*      syncMask     = nullptr;
    QSpinBox*       bitsPerFrame = nullptr;
    QDoubleSpinBox* dataRate     = nullptr;
    QComboBox*      sampleRate   = nullptr;
};

/// Builds the standard frame-sync input block — Frame Sync / Mask / Bits Per
/// Frame on rows 0-1 and Data Rate / Average Period on rows 3-4 — into @p grid
/// and returns the widgets (columns 4+ are left free for the caller's Load/Save
/// buttons). The two dialogs differ only in the sync field's placeholder and
/// tooltip, which are passed in.
FrameSyncWidgets buildFrameSyncRow(QGridLayout* grid, QWidget* parent,
                                   const StreamConfig& cfg,
                                   const QString& syncPlaceholder,
                                   const QString& syncTooltip)
{
    FrameSyncWidgets w;

    w.syncPattern = new QLineEdit(parent);
    w.syncPattern->setValidator(new QRegularExpressionValidator(kHexRegex, parent));
    w.syncPattern->setText(cfg.sync.pattern);
    w.syncPattern->setPlaceholderText(syncPlaceholder);
    w.syncPattern->setMinimumWidth(110);
    w.syncPattern->setToolTip(syncTooltip);
    grid->addWidget(new QLabel("Frame Sync"),      0, 0, Qt::AlignLeft | Qt::AlignVCenter);
    grid->addWidget(w.syncPattern,                 1, 0, Qt::AlignLeft | Qt::AlignVCenter);

    w.syncMask = new QLineEdit(parent);
    w.syncMask->setValidator(new QRegularExpressionValidator(kHexRegex, parent));
    w.syncMask->setText(cfg.sync.mask);
    w.syncMask->setPlaceholderText("e.g. FFFFFFFF");
    w.syncMask->setMinimumWidth(110);
    w.syncMask->setToolTip("Hex mask applied during frame sync comparison. Set bits are compared; cleared bits are ignored.");
    grid->addWidget(new QLabel("Frame Sync Mask"), 0, 1, Qt::AlignLeft | Qt::AlignVCenter);
    grid->addWidget(w.syncMask,                    1, 1, Qt::AlignLeft | Qt::AlignVCenter);

    w.bitsPerFrame = new QSpinBox(parent);
    w.bitsPerFrame->setRange(PCMConstants::kMinFrameLengthBits,
                             PCMConstants::kMaxFrameLengthBits);
    w.bitsPerFrame->setValue(cfg.sync.bitsInMinorFrame);
    w.bitsPerFrame->setMinimumWidth(80);
    w.bitsPerFrame->setToolTip("Total number of bits in one minor frame, including the frame sync word.");
    grid->addWidget(new QLabel("Bits Per Frame"),  0, 2, Qt::AlignLeft | Qt::AlignVCenter);
    grid->addWidget(w.bitsPerFrame,                1, 2, Qt::AlignLeft | Qt::AlignVCenter);

    // Spacer row between the frame-sync inputs and the rate/period group.
    grid->setRowMinimumHeight(2, DialogLayout::kControlGap);

    w.dataRate = new QDoubleSpinBox(parent);
    w.dataRate->setRange(0.0, 1000.0);
    w.dataRate->setDecimals(3);
    w.dataRate->setSingleStep(0.1);
    if (cfg.tmatsDataRateMbps > 0.0)
        w.dataRate->setSpecialValueText(QString::number(cfg.tmatsDataRateMbps, 'f', 3));
    else
        w.dataRate->setSpecialValueText("TMATS");
    w.dataRate->setValue(cfg.sync.dataRateMbps);
    w.dataRate->setMinimumWidth(130);
    w.dataRate->setToolTip("Telemetry data rate in Mbps. Set to 0 (TMATS) to derive the rate from the file metadata.");
    // QDoubleSpinBox's natural sizeHint runs taller than QLineEdit's (its up/down
    // buttons need more room than the shared padding alone provides), while the
    // combo beside it is explicitly matched to w.syncPattern below — match here
    // too so Data Rate and Average Period render at the same height.
    matchControlHeight(w.dataRate, w.syncPattern);
    grid->addWidget(new QLabel("Data Rate (Mbps)"), 3, 0, Qt::AlignLeft | Qt::AlignVCenter);
    grid->addWidget(w.dataRate,                     4, 0, Qt::AlignLeft | Qt::AlignVCenter);

    w.sampleRate = new QComboBox(parent);
    w.sampleRate->addItem(periodText(UIConstants::kSamplePeriod1s));
    w.sampleRate->addItem(periodText(UIConstants::kSamplePeriod100ms));
    w.sampleRate->addItem(periodText(UIConstants::kSamplePeriod10ms));
    w.sampleRate->setCurrentIndex(cfg.samplePeriodIndex);
    w.sampleRate->setToolTip("Integration window for statistics. Shorter periods give finer time resolution; longer periods smooth noise.");
    matchControlHeight(w.sampleRate, w.syncPattern);
    grid->addWidget(new QLabel("Average Period"),  3, 1, Qt::AlignLeft | Qt::AlignVCenter);
    grid->addWidget(w.sampleRate,                  4, 1, Qt::AlignLeft | Qt::AlignVCenter);

    return w;
}

/// Cancel + primary action button pair shared by every stream dialog.
struct DialogButtons
{
    QPushButton* cancel  = nullptr;
    QPushButton* primary = nullptr;
};

/// Creates a Cancel button (pre-wired to reject @p dialog) and a blue, default
/// primary button labelled @p primaryText. The caller wires the primary button's
/// click handler and arranges both in a layout.
DialogButtons makeDialogButtons(QDialog* dialog, const QString& primaryText)
{
    DialogButtons b;
    b.cancel  = new QPushButton(QObject::tr("Cancel"), dialog);
    b.primary = new QPushButton(primaryText, dialog);
    b.primary->setStyleSheet(kBlueButtonStyle);
    b.primary->setDefault(true);
    QObject::connect(b.cancel, &QPushButton::clicked, dialog, &QDialog::reject);
    return b;
}

/// Applies the shared horizontal/vertical spacing to a dialog form grid, so every
/// grid across the stream dialogs lines up on the same rhythm.
void configureFormGrid(QGridLayout* grid)
{
    grid->setHorizontalSpacing(DialogLayout::kGridHSpacing);
    grid->setVerticalSpacing(DialogLayout::kGridVSpacing);
}

/// Reserves the standard gap column to the right of a form grid's input columns
/// (0-2) and drops the Load/Save icon buttons into the canonical cells. Callers no
/// longer hard-code the gap-column index or the (row 1, col 4/5) button offsets —
/// the one place that knows the frame-sync grid geometry.
void addLoadSaveButtons(QGridLayout* grid, QPushButton* loadBtn, QPushButton* saveBtn)
{
    constexpr int kGapCol = 3, kLoadCol = 4, kSaveCol = 5, kButtonRow = 1;
    grid->setColumnMinimumWidth(kGapCol, DialogLayout::kColGapWidth);
    grid->setColumnStretch(kGapCol, 1);
    grid->addWidget(loadBtn, kButtonRow, kLoadCol, Qt::AlignVCenter);
    grid->addWidget(saveBtn, kButtonRow, kSaveCol, Qt::AlignVCenter);
}

/// Appends a left-aligned "[checkbox] label" row (with a trailing stretch) to a
/// vertical layout. Shared by the Derandomize / Invert Data toggles.
void addCheckboxRow(QVBoxLayout* layout, QWidget* parent, QCheckBox* box, const QString& text)
{
    auto* row = new QHBoxLayout;
    row->setSpacing(DialogLayout::kCheckboxLabelGap);
    row->addWidget(box);
    row->addWidget(new QLabel(text, parent));
    row->addStretch(1);
    layout->addLayout(row);
}

/// Appends the standard dialog footer: an optional left-aligned toggle (with an
/// optional adjacent label), a stretch, then the primary button and Cancel. Used
/// by every stream dialog so the footer is built one way.
void addBottomBar(QVBoxLayout* layout, const DialogButtons& buttons, QWidget* parent,
                  QCheckBox* leftToggle = nullptr, const QString& toggleLabel = QString())
{
    auto* bar = new QHBoxLayout;
    if (leftToggle != nullptr)
    {
        bar->addWidget(leftToggle);
        if (!toggleLabel.isEmpty())
            bar->addWidget(new QLabel(toggleLabel, parent));
    }
    bar->addStretch(1);
    // Primary ("do it") action leftmost, Cancel (safe/dismissive) rightmost —
    // matches Microsoft's WinUI3 dialog button-order guidance.
    bar->addWidget(buttons.primary);
    bar->addWidget(buttons.cancel);
    layout->addLayout(bar);
}

////////////////////////////////////////////////////////////////////////////////
//                        FRAME LOCK SETUP DIALOG                             //
////////////////////////////////////////////////////////////////////////////////

class FrameLockSetupDialog : public QDialog
{
public:
    explicit FrameLockSetupDialog(const StreamConfig& cfg,
                                  const QString& toml_dir,
                                  const QString& app_root,
                                  QWidget* parent = nullptr)
        : QDialog(parent)
        , m_toml_dir(toml_dir)
        , m_app_root(app_root)
    {
        setWindowTitle("Configure Frame Sync Lock — " + cfg.label);
        setModal(true);

        // Frame Sync / Mask / Bits + Data Rate / Average Period, then the Load/Save
        // icon buttons in the gap column addLoadSaveButtons owns.
        auto* grid = new QGridLayout;
        configureFormGrid(grid);

        m_fs = buildFrameSyncRow(grid, this, cfg,
            "e.g. A345CA5C",
            "Hex frame synchronization word (e.g. A345CA5C). Used to detect frame boundaries in the PCM stream.");

        auto* loadBtn = new QPushButton(this);
        loadBtn->setIcon(QIcon(":/resources/folder-open.svg"));
        loadBtn->setToolTip("Load frame sync fields from a TOML file");
        styleIconButton(loadBtn, DialogLayout::kIconButtonSize);

        auto* saveBtn = new QPushButton(this);
        saveBtn->setIcon(QIcon(":/resources/floppy-save.svg"));
        saveBtn->setToolTip("Save frame sync fields to a TOML file");
        styleIconButton(saveBtn, DialogLayout::kIconButtonSize);

        addLoadSaveButtons(grid, loadBtn, saveBtn);

        connect(loadBtn, &QPushButton::clicked, this, [this]() {
            QString filename = QFileDialog::getOpenFileName(
                this, tr("Load Frame Sync Parameters"),
                settingsSubdir(m_app_root, UIConstants::kFramesyncPatternsDirName, m_toml_dir),
                tr("TOML Files (*.toml);;All Files (*.*)"));
            if (!filename.isEmpty())
                loadFrameSyncFromToml(filename, m_fs.syncPattern, m_fs.syncMask,
                                      m_fs.bitsPerFrame, m_toml_dir, m_inverted);
        });
        connect(saveBtn, &QPushButton::clicked, this, [this]() {
            QString filename = QFileDialog::getSaveFileName(
                this, tr("Save Frame Sync Parameters"), m_toml_dir,
                tr("TOML Files (*.toml);;All Files (*.*)"));
            if (filename.isEmpty()) return;
            if (QFileInfo(filename).suffix().isEmpty()) filename += ".toml";
            saveFrameSyncToToml(filename,
                                m_fs.syncPattern->text().trimmed().toUpper(),
                                m_fs.syncMask->text().trimmed().toUpper(),
                                m_fs.bitsPerFrame->value(),
                                m_inverted->isChecked(),
                                m_toml_dir);
        });

        auto* outer = new QVBoxLayout(this);
        outer->setSpacing(DialogLayout::kOuterSpacing);
        outer->addLayout(grid);
        outer->addSpacing(DialogLayout::kSectionGap);

        m_randomized = new QCheckBox(this);
        m_randomized->setChecked(cfg.sync.randomized);
        m_randomized->setToolTip("Apply RNRZ-L self-synchronizing descrambler to the data stream.");
        addCheckboxRow(outer, this, m_randomized, "Derandomize");

        m_inverted = new QCheckBox(this);
        m_inverted->setChecked(cfg.sync.inverted);
        m_inverted->setToolTip("Invert every bit of the raw data stream before processing (use when the PCM signal polarity is inverted).");
        addCheckboxRow(outer, this, m_inverted, "Invert Data");

        outer->addStretch(1);
        addSeparator(outer, this);

        DialogButtons btns = makeDialogButtons(this, tr("OK"));
        connect(btns.primary, &QPushButton::clicked, this, [this]() {
            if (m_fs.syncPattern->text().trimmed().isEmpty())
            {
                QMessageBox::warning(this, tr("Missing Frame Sync"),
                    tr("A frame sync pattern is required (e.g. FE6B2840)."));
                return;
            }
            accept();
        });

        m_apply_to_all = new QCheckBox(this);
        m_apply_to_all->setToolTip("Copy these settings to every other selected "
                                  "stream currently set to Frame Sync Lock mode.");
        addBottomBar(outer, btns, this, m_apply_to_all, "Apply to all Frame Sync Lock streams");

        adjustSize();
    }

    QString frameSyncPattern() const { return m_fs.syncPattern->text().trimmed().toUpper(); }
    QString frameSyncMask()    const { return m_fs.syncMask->text().trimmed().toUpper(); }
    int     bitsPerFrame()     const { return m_fs.bitsPerFrame->value(); }
    bool    randomized()        const { return m_randomized->isChecked(); }
    bool    inverted()          const { return m_inverted->isChecked(); }
    int     samplePeriodIndex() const { return m_fs.sampleRate->currentIndex(); }
    double  dataRateMbps()      const { return m_fs.dataRate->value(); }
    QString lastTomlDir()       const { return m_toml_dir; }
    bool    applyToAll()        const { return m_apply_to_all->isChecked(); }

private:
    FrameSyncWidgets m_fs; ///< Frame Sync / Mask / Bits Per Frame / Data Rate / Average Period.
    QCheckBox*      m_randomized   = nullptr;
    QCheckBox*      m_inverted     = nullptr;
    QCheckBox*      m_apply_to_all   = nullptr;
    QString         m_toml_dir;
    QString         m_app_root;
};

////////////////////////////////////////////////////////////////////////////////
//                      CALIBRATION SETUP DIALOG (US5.3)                       //
////////////////////////////////////////////////////////////////////////////////

/// Collects the two files needed for non-linear calibration extraction — the
/// step-config TOML and the calibration Chapter 10 file — and produces the
/// per-channel calibration profiles directly:
///   * Step config: the [[Step]] table format must parse.
///   * Cal Ch10: as soon as both files are present, the whole file is processed
///     (CalibrationExtractor) to detect the step plateaus and build the
///     profiles. This is the single, authoritative pass — there is no separate
///     pre-flight lock check and nothing further to process on OK.
/// OK is enabled once extraction has produced at least one valid profile. The
/// owner reads calibrationByWord() on accept.
class CalibrationSetupDialog : public QDialog
{
public:
    /// @param baseRequest Acquisition + receiver fields already filled by the
    ///        caller (frame sync, channel IDs, word-map source). The dialog adds
    ///        the cal file, parsed steps, and clip seconds before extracting.
    /// @param initialStepPath The step file this stream last used, if any. The
    ///        dialog opens with it, or with the shipped default if it is empty or
    ///        gone (StepDetector::initialStepConfigPath).
    CalibrationSetupDialog(const CalibrationExtractor::Request& baseRequest,
                           const QString& tomlDir,
                           const QString& appRoot,
                           const QString& initialStepPath = QString(),
                           QWidget* parent = nullptr)
        : QDialog(parent)
        , m_base_request(baseRequest)
        , m_toml_dir(tomlDir)
        , m_app_root(appRoot)
    {
        setWindowTitle("Extract Calibration");
        setModal(true);

        auto* outer = new QVBoxLayout(this);
        outer->setSpacing(DialogLayout::kOuterSpacing);

        auto* grid = new QGridLayout;
        configureFormGrid(grid);
        grid->setColumnStretch(1, 1); // let the path-label column absorb slack

        // ---- Row 0/1: Step Cal file -----------------------------------------
        grid->addWidget(new QLabel("Step Cal File:"), 0, 0);
        m_step_path_label = new QLabel(this);
        m_step_path_label->setMinimumWidth(260);
        grid->addWidget(m_step_path_label, 0, 1);
        auto* stepBrowse = new QPushButton("Browse...", this);
        stepBrowse->setToolTip("Browse for the step calibration configuration TOML file that defines the RF signal step levels.");
        grid->addWidget(stepBrowse, 0, 2);
        m_step_status = new QLabel(this);
        grid->addWidget(m_step_status, 1, 1, 1, 2);

        // ---- Row 2/3: Calibration Ch10 file ---------------------------------
        grid->setRowMinimumHeight(2, DialogLayout::kControlGap);
        grid->addWidget(new QLabel("Calibration Ch10:"), 3, 0);
        m_cal_path_label = new QLabel(this);
        m_cal_path_label->setMinimumWidth(260);
        grid->addWidget(m_cal_path_label, 3, 1);
        m_cal_browse = new QPushButton("Browse...", this);
        m_cal_browse->setToolTip("Browse for the calibration Chapter 10 recording file. The last folder you used will be remembered.");
        grid->addWidget(m_cal_browse, 3, 2);
        m_cal_status = new QLabel(this);
        grid->addWidget(m_cal_status, 4, 1, 1, 2);

        // ---- Rows 5-7: Clip Start / Clip End (seconds), stacked ----------------
        // Ignore the first/last N seconds of the cal recording before detecting
        // steps, so signal-generator turn-on transients (or trailing junk) don't
        // get mistaken for calibration plateaus.
        {
            // Breathing room between the cal-file status and the clip controls.
            grid->addItem(new QSpacerItem(0, DialogLayout::kSectionGap,
                                          QSizePolicy::Minimum, QSizePolicy::Fixed), 5, 0);

            grid->addWidget(new QLabel("Clip Start (s):", this), 6, 0);
            m_clip_start = new QDoubleSpinBox(this);
            m_clip_start->setRange(0.0, 100000.0);
            m_clip_start->setDecimals(1);
            m_clip_start->setSingleStep(1.0);
            m_clip_start->setValue(0.0);
            m_clip_start->setFixedWidth(100);
            m_clip_start->setToolTip("Ignore this many seconds at the START of the "
                "calibration recording before detecting steps (skips signal-generator "
                "turn-on transients).");
            grid->addWidget(m_clip_start, 6, 1, Qt::AlignLeft);

            grid->addWidget(new QLabel("Clip End (s):", this), 7, 0);
            m_clip_end = new QDoubleSpinBox(this);
            m_clip_end->setRange(0.0, 100000.0);
            m_clip_end->setDecimals(1);
            m_clip_end->setSingleStep(1.0);
            m_clip_end->setValue(0.0);
            m_clip_end->setFixedWidth(100);
            m_clip_end->setToolTip("Ignore this many seconds at the END of the "
                "calibration recording before detecting steps.");
            grid->addWidget(m_clip_end, 7, 1, Qt::AlignLeft);

            // Re-run extraction when the user changes a clip value (only fires if
            // both files are already loaded).
            connect(m_clip_start, &QDoubleSpinBox::editingFinished,
                    this, [this]() { maybeRunExtraction(); });
            connect(m_clip_end, &QDoubleSpinBox::editingFinished,
                    this, [this]() { maybeRunExtraction(); });
        }

        outer->addLayout(grid);

        connect(stepBrowse,  &QPushButton::clicked, this, [this]() { onBrowseStep(); });
        connect(m_cal_browse, &QPushButton::clicked, this, [this]() { onBrowseCal(); });

        addSeparator(outer, this);

        DialogButtons calBtns = makeDialogButtons(this, tr("OK"));
        m_ok_button = calBtns.primary;
        connect(m_ok_button, &QPushButton::clicked, this, &QDialog::accept);
        addBottomBar(outer, calBtns, this);

        setStatus(m_step_status, Pending, "No file selected.");
        setStatus(m_cal_status,  Pending, "No file selected.");

        // Almost every calibration uses the same step file, so start with one
        // loaded. The status says it was loaded automatically: a step file that
        // does not match the recording still calibrates, just wrongly, so the
        // operator must be able to see which file is in play without browsing.
        const QString initialStep =
            StepDetector::initialStepConfigPath(m_app_root, initialStepPath);
        if (!initialStep.isEmpty())
        {
            loadStepFile(initialStep, /*automatic=*/true);
        }

        updateOk();
        adjustSize();
    }

    /// Per-channel calibration profiles produced by the whole-file extraction,
    /// keyed by word index. Valid after the dialog is accepted.
    QHash<int, CalibrationProfile> calibrationByWord() const { return m_calibration_by_word; }
    QString lastTomlDir() const { return m_toml_dir; }

    /// Operator-facing report naming which receivers calibrated and why the rest
    /// fell back. Valid after the dialog is accepted.
    QString calibrationReport() const { return m_calibration_report; }

    // Input references: what produced the profiles above, so a processing template
    // can store them for a later re-extraction pass.
    QString calFilePath()  const { return m_cal_path; }
    QString stepFilePath() const { return m_step_path; }
    double  clipStartSec() const { return m_clip_start ? m_clip_start->value() : 0.0; }
    double  clipEndSec()   const { return m_clip_end   ? m_clip_end->value()   : 0.0; }

private:
    enum StatusKind { Pending, Ok, Warn, Fail };

    void setStatus(QLabel* label, StatusKind kind, const QString& text)
    {
        QString prefix;
        QString color;
        switch (kind)
        {
            case Ok:      prefix = "✓ "; color = "green"; break;  // checkmark
            case Warn:    prefix = "⚠ "; color = "#DAA520"; break; // the log's warning colour
            case Fail:    prefix = "✗ "; color = "#cc0000"; break; // cross
            case Pending: default: color = "gray"; break;
        }
        label->setText(QString("<span style='color: %1;'>%2%3</span>")
                           .arg(color, prefix, text.toHtmlEscaped()));
    }

    void updateOk()
    {
        if (m_ok_button != nullptr)
        {
            m_ok_button->setEnabled(m_step_ok && m_cal_ok);
        }
    }

    void onBrowseStep()
    {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Select Step Configuration (TOML)"),
            settingsSubdir(m_app_root, UIConstants::kRcvrCalsDirName, m_toml_dir),
            tr("TOML Files (*.toml);;All Files (*.*)"));
        if (path.isEmpty()) return;

        m_toml_dir = QFileInfo(path).absolutePath();
        loadStepFile(path, /*automatic=*/false);
    }

    /// Selects @p path as the step file and parses it. @p automatic marks a file
    /// the dialog preloaded rather than one the operator chose, so the status can
    /// say so.
    void loadStepFile(const QString& path, bool automatic)
    {
        m_step_path = path;
        m_step_path_label->setText(QFileInfo(path).fileName());

        QVector<StepDefinition> steps;
        QString error;
        if (StepDetector::parseStepConfig(path, steps, error))
        {
            m_steps  = steps;
            m_step_ok = true;
            setStatus(m_step_status, Ok,
                      QString("%1 steps parsed%2.")
                          .arg(steps.size())
                          .arg(automatic ? QStringLiteral(" (loaded automatically)") : QString()));
        }
        else
        {
            m_steps.clear();
            m_step_ok = false;
            setStatus(m_step_status, Fail, error);
        }
        updateOk();
        maybeRunExtraction();
    }

    void onBrowseCal()
    {
        if (m_base_request.sync.pattern.trimmed().isEmpty())
        {
            QMessageBox::warning(this, tr("Missing Frame Sync"),
                tr("Set a frame sync pattern before loading the calibration file."));
            return;
        }

        QSettings settings;
        const QString lastCalDir = settings.value("CalibrationSetupDialog/lastCalDir").toString();

        const QString path = QFileDialog::getOpenFileName(
            this, tr("Select Calibration Chapter 10 File"),
            lastCalDir.isEmpty() ? m_toml_dir : lastCalDir,
            tr("Chapter 10 Files (*.ch10 *.c10);;All Files (*.*)"));
        if (path.isEmpty()) return;

        settings.setValue("CalibrationSetupDialog/lastCalDir", QFileInfo(path).absolutePath());

        m_cal_path = path;
        m_cal_path_label->setText(QFileInfo(path).fileName());
        maybeRunExtraction();
    }

    /// Runs the whole-file calibration extraction once both inputs are present.
    /// This is the single processing pass: it detects the step plateaus and
    /// builds the per-channel profiles. OK simply hands these back to the owner.
    void maybeRunExtraction()
    {
        if (m_cal_path.isEmpty())
        {
            return; // Nothing to do until a cal file is chosen.
        }
        if (!m_step_ok)
        {
            m_cal_ok = false;
            setStatus(m_cal_status, Pending,
                      tr("Select a valid step cal file to process this recording."));
            updateOk();
            return;
        }

        m_cal_ok = false;
        m_calibration_by_word.clear();
        setStatus(m_cal_status, Pending, tr("Processing calibration file…"));
        updateOk();

        // Start from the caller-supplied acquisition/receiver fields and add the
        // cal file, parsed steps, and clip seconds the dialog collected.
        CalibrationExtractor::Request req = m_base_request;
        req.calFilename        = m_cal_path;
        req.steps              = m_steps;
        req.clipStartSec       = m_clip_start ? m_clip_start->value() : 0.0;
        req.clipEndSec         = m_clip_end ? m_clip_end->value() : 0.0;
        // Resolve the word map the SAME way the main processing run does
        // (mainviewmodel buildJob): when the user hasn't picked an explicit
        // Receiver Parameters file, fall back to the shipped default.toml rather
        // than letting the extractor synthesize a sequential grid. The two passes
        // MUST agree on word indices, because the resulting profiles are matched
        // to plot channels by word — a divergent map silently misattaches every
        // profile and the plot falls back to linear calibration.
        if (req.receiverParamsToml.isEmpty())
        {
            const QString defaultRcvrParams = m_app_root + "/" + UIConstants::kSettingsDirName +
                "/" + UIConstants::kReceiverParamsDirName + "/" + UIConstants::kDefaultTomlFilename;
            if (QFileInfo::exists(defaultRcvrParams))
            {
                req.receiverParamsToml = defaultRcvrParams;
            }
        }

        CalibrationExtractor extractor;
        QProgressDialog progress(tr("Processing calibration file…"), tr("Cancel"),
                                 0, 100, this);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        progress.setAutoClose(false);
        progress.setAutoReset(false);

        connect(&extractor, &CalibrationExtractor::progressChanged,
                &progress, &QProgressDialog::setValue);
        connect(&progress, &QProgressDialog::canceled, this, [&]() {
            progress.setLabelText(tr("Cancelling…"));
            progress.setCancelButton(nullptr);
            extractor.cancel();
        });

        QEventLoop loop;
        bool success = false;
        bool finishedAlready = false;
        QString summary;
        connect(&extractor, &CalibrationExtractor::finished, this,
                [&](bool ok, const QString& msg) {
                    success = ok;
                    summary = msg;
                    finishedAlready = true;
                    loop.quit();
                });

        extractor.start(req);
        if (!finishedAlready)
        {
            progress.show();
            loop.exec();
        }
        progress.close();

        if (!success)
        {
            setStatus(m_cal_status, Fail, summary);
            updateOk();
            return;
        }

        int valid = 0;
        for (const CalibrationChannelResult& r : extractor.results())
        {
            if (r.profile.valid)
            {
                m_calibration_by_word.insert(r.word, r.profile);
                valid++;
            }
        }
        // Built here, while the per-channel outcomes are still in hand:
        // m_calibration_by_word keeps only the successes (keyed by word), so it
        // cannot say which receivers fell back or why once the extractor is gone.
        m_calibration_report = CalibrationExtractor::summarize(extractor.results(),
                                                              m_steps.size());

        m_cal_ok = (valid > 0);
        if (extractor.clipIgnored())
        {
            // The clips are silently dropped when they would leave nothing to
            // measure, so without this the operator sees a result that ignored the
            // values they typed and has no way to tell.
            const QString warning =
                tr("Clip Start + Clip End (%1 s) exceed the recording (%2 s) - clips ignored.")
                    .arg(clipStartSec() + clipEndSec(), 0, 'f', 1)
                    .arg(extractor.recordingSeconds(), 0, 'f', 1);
            m_calibration_report.prepend(warning + "\n\n");
            setStatus(m_cal_status, Warn, warning + " " + summary);
        }
        else
        {
            setStatus(m_cal_status, m_cal_ok ? Ok : Fail, summary);
        }
        updateOk();
    }

    // Caller-supplied acquisition + receiver settings forwarded to the extractor;
    // the dialog only adds the cal file, steps, and clip seconds before running.
    CalibrationExtractor::Request m_base_request;
    QString m_toml_dir;
    QString m_app_root;

    // Selected files + parsed steps.
    QString m_step_path;
    QString m_cal_path;
    QVector<StepDefinition> m_steps;
    bool    m_step_ok = false;
    bool    m_cal_ok  = false;

    // Extracted profiles, keyed by word index (populated by maybeRunExtraction).
    QHash<int, CalibrationProfile> m_calibration_by_word;
    QString m_calibration_report; ///< Receiver-grouped outcome report for the summary box.

    // Widgets.
    QLabel*      m_step_path_label = nullptr;
    QLabel*      m_step_status    = nullptr;
    QLabel*      m_cal_path_label  = nullptr;
    QLabel*      m_cal_status     = nullptr;
    QPushButton* m_cal_browse     = nullptr;
    QPushButton* m_ok_button      = nullptr;
    QDoubleSpinBox* m_clip_start  = nullptr; ///< Seconds to clip from the start before step detection.
    QDoubleSpinBox* m_clip_end    = nullptr; ///< Seconds to clip from the end before step detection.
};

////////////////////////////////////////////////////////////////////////////////
//                         RECEIVER SNR DIALOG                                //
////////////////////////////////////////////////////////////////////////////////

class ReceiverSNRDialog : public QDialog
{
public:
    explicit ReceiverSNRDialog(const StreamConfig& cfg,
                               const QString& toml_dir,
                               int time_channel_id,
                               const QString& app_root,
                               bool swap_bytes,
                               QWidget* parent = nullptr)
        : QDialog(parent)
        , m_receiver_params_toml(cfg.receiverParamsToml)
        , m_toml_dir(toml_dir)
        , m_app_root(app_root)
        , m_time_channel_id(time_channel_id)
        , m_swap_bytes(swap_bytes)
        , m_pcm_channel_id(cfg.pcmChannelId)
        , m_calibration_by_word(cfg.calibrationByWord)
        , m_cal_ch10_path(cfg.calCh10Path)
        , m_step_toml_path(cfg.stepTomlPath)
        , m_clip_start_sec(cfg.clipStartSec)
        , m_clip_end_sec(cfg.clipEndSec)
    {
        setWindowTitle("Configure Receiver SNR — " + cfg.label);
        setModal(true);

        auto* outer = new QVBoxLayout(this);
        outer->setSpacing(DialogLayout::kOuterSpacing);

        // ---- Group 1: Frame sync + acquisition settings --------------------
        {
            auto* grid = new QGridLayout;
            configureFormGrid(grid);

            // Frame Sync / Mask / Bits + Data Rate / Average Period (rows 0-4).
            m_fs = buildFrameSyncRow(grid, this, cfg,
                "e.g. FE6B2840",
                "Hex frame synchronization word (e.g. FE6B2840). Used to detect frame boundaries in the receiver PCM stream.");

            auto* loadBtn1 = new QPushButton(this);
            loadBtn1->setIcon(QIcon(":/resources/folder-open.svg"));
            loadBtn1->setToolTip("Load frame sync fields from a TOML file");
            styleIconButton(loadBtn1, DialogLayout::kIconButtonSize);
            auto* saveBtn1 = new QPushButton(this);
            saveBtn1->setIcon(QIcon(":/resources/floppy-save.svg"));
            saveBtn1->setToolTip("Save frame sync fields to a TOML file");
            styleIconButton(saveBtn1, DialogLayout::kIconButtonSize);
            addLoadSaveButtons(grid, loadBtn1, saveBtn1);

            connect(loadBtn1, &QPushButton::clicked, this, [this]() {
                QString filename = QFileDialog::getOpenFileName(
                    this, tr("Load Frame Sync Parameters"),
                    settingsSubdir(m_app_root, UIConstants::kFramesyncPatternsDirName, m_toml_dir),
                    tr("TOML Files (*.toml);;All Files (*.*)"));
                if (!filename.isEmpty())
                    loadFrameSyncFromToml(filename, m_fs.syncPattern, m_fs.syncMask,
                                         m_fs.bitsPerFrame, m_toml_dir, m_inverted);
            });
            connect(saveBtn1, &QPushButton::clicked, this, [this]() {
                QString filename = QFileDialog::getSaveFileName(
                    this, tr("Save Frame Sync Parameters"), m_toml_dir,
                    tr("TOML Files (*.toml);;All Files (*.*)"));
                if (filename.isEmpty()) return;
                if (QFileInfo(filename).suffix().isEmpty()) filename += ".toml";
                saveFrameSyncToToml(filename,
                                    m_fs.syncPattern->text().trimmed().toUpper(),
                                    m_fs.syncMask->text().trimmed().toUpper(),
                                    m_fs.bitsPerFrame->value(),
                                    m_inverted->isChecked(),
                                    m_toml_dir);
            });

            outer->addLayout(grid);
        }

        outer->addSpacing(DialogLayout::kSectionGap);

        m_randomized = new QCheckBox(this);
        m_randomized->setChecked(cfg.sync.randomized);
        m_randomized->setToolTip("Apply RNRZ-L self-synchronizing descrambler to the data stream.");
        addCheckboxRow(outer, this, m_randomized, "Derandomize");

        m_inverted = new QCheckBox(this);
        m_inverted->setChecked(cfg.sync.inverted);
        m_inverted->setToolTip("Invert every bit of the raw data stream before processing (use when the PCM signal polarity is inverted).");
        addCheckboxRow(outer, this, m_inverted, "Invert Data");

        // ---- Separator ------------------------------------------------------
        addSeparator(outer, this);

        // ---- Group 2: Receiver calibration parameters ----------------------
        // Text inputs (combos/spin boxes) left-aligned so they line up with the
        // frame-sync block above; icon buttons stay centered in their cells.
        {
            auto* grid = new QGridLayout;
            configureFormGrid(grid);
            const auto kFieldAlign = Qt::Alignment(Qt::AlignLeft | Qt::AlignVCenter);

            // Row 0: labels
            grid->addWidget(new QLabel("Polarity"),      0, 0, kFieldAlign);
            grid->addWidget(new QLabel("Slope"),         0, 1, kFieldAlign);
            grid->addWidget(new QLabel("Scale (dB/V)"),  0, 2, kFieldAlign);

            // Row 1: inputs
            m_polarity = new QComboBox(this);
            m_polarity->addItem("Positive");
            m_polarity->addItem("Negative");
            m_polarity->setCurrentIndex(cfg.polarityIndex);
            m_polarity->setToolTip("ADC output polarity. Positive = high voltage maps to highest dB; Negative = inverted.");
            matchControlHeight(m_polarity, m_fs.syncPattern);
            grid->addWidget(m_polarity, 1, 0, kFieldAlign);

            m_slope = new QComboBox(this);
            m_slope->addItem("±10 V");
            m_slope->addItem("±5 V");
            m_slope->addItem("0–10 V");
            m_slope->addItem("0–5 V");
            m_slope->setCurrentIndex(cfg.slopeIndex);
            m_slope->setToolTip("ADC voltage range. Select the range that matches your receiver's analog output voltage span.");
            matchControlHeight(m_slope, m_fs.syncPattern);
            grid->addWidget(m_slope, 1, 1, kFieldAlign);

            m_scale = new QDoubleSpinBox(this);
            m_scale->setRange(0.001, 999.999);
            m_scale->setDecimals(3);
            m_scale->setValue(cfg.scaleDdBPerV);
            m_scale->setMinimumWidth(100);
            m_scale->setToolTip("Calibration scale factor in dB/V. Maps the ADC voltage span to the receiver SNR range in dB.");
            grid->addWidget(m_scale, 1, 2, kFieldAlign);

            // Row 2: spacer
            grid->setRowMinimumHeight(2, DialogLayout::kControlGap);

            // Row 3: labels
            grid->addWidget(new QLabel("Num Rcvrs"),    3, 0, kFieldAlign);
            grid->addWidget(new QLabel("Num Channels"), 3, 1, kFieldAlign);

            // Row 4: inputs
            m_num_receivers = new QSpinBox(this);
            m_num_receivers->setRange(1, 100);
            m_num_receivers->setValue(cfg.numReceivers);
            m_num_receivers->setToolTip("Number of individual receiver units contributing channels to this stream.");
            grid->addWidget(m_num_receivers, 4, 0, kFieldAlign);

            m_receiver_channels = new QSpinBox(this);
            m_receiver_channels->setRange(1, 100);
            m_receiver_channels->setValue(cfg.receiverChannels);
            m_receiver_channels->setToolTip("Number of channels per receiver (e.g. 3 for L/R/C configuration).");
            grid->addWidget(m_receiver_channels, 4, 1, kFieldAlign);

            auto* loadBtn2 = new QPushButton(this);
            loadBtn2->setIcon(QIcon(":/resources/folder-open.svg"));
            loadBtn2->setToolTip("Load receiver parameters from a TOML file");
            styleIconButton(loadBtn2, DialogLayout::kIconButtonSize);
            auto* saveBtn2 = new QPushButton(this);
            saveBtn2->setIcon(QIcon(":/resources/floppy-save.svg"));
            saveBtn2->setToolTip("Save receiver parameters to a TOML file");
            styleIconButton(saveBtn2, DialogLayout::kIconButtonSize);
            addLoadSaveButtons(grid, loadBtn2, saveBtn2);

            connect(loadBtn2, &QPushButton::clicked, this, [this]() {
                QString filename = QFileDialog::getOpenFileName(
                    this, tr("Load Receiver Parameters"),
                    settingsSubdir(m_app_root, UIConstants::kReceiverParamsDirName, m_toml_dir),
                    tr("TOML Files (*.toml);;All Files (*.*)"));
                if (filename.isEmpty()) return;
                m_toml_dir = QFileInfo(filename).absolutePath();
                m_receiver_params_toml = filename;

                // The file's two halves are read by the Model: the scalars here,
                // the word map by FrameSetup::tryLoadingFile() when the stream is
                // processed. The dialog only displays what it is handed. What is on
                // screen goes in as the defaults, so a file that omits a key leaves
                // that field alone rather than resetting it.
                ReceiverParams current;
                current.polarityIndex    = m_polarity->currentIndex();
                current.slopeIndex       = m_slope->currentIndex();
                current.scaleDdBPerV     = m_scale->value();
                current.numReceivers     = m_num_receivers->value();
                current.receiverChannels = m_receiver_channels->value();

                const ReceiverParams params =
                    FrameSetup::readReceiverParams(filename, current);
                m_polarity->setCurrentIndex(params.polarityIndex);
                m_slope->setCurrentIndex(params.slopeIndex);
                m_scale->setValue(params.scaleDdBPerV);
                m_num_receivers->setValue(params.numReceivers);
                m_receiver_channels->setValue(params.receiverChannels);
            });
            connect(saveBtn2, &QPushButton::clicked, this, [this]() {
                QString filename = QFileDialog::getSaveFileName(
                    this, tr("Save Receiver Parameters"), m_toml_dir,
                    tr("TOML Files (*.toml);;All Files (*.*)"));
                if (filename.isEmpty()) return;
                if (QFileInfo(filename).suffix().isEmpty()) filename += ".toml";
                m_toml_dir = QFileInfo(filename).absolutePath();

                ReceiverParams params;
                params.polarityIndex    = m_polarity->currentIndex();
                params.slopeIndex       = m_slope->currentIndex();
                params.scaleDdBPerV     = m_scale->value();
                params.numReceivers     = m_num_receivers->value();
                params.receiverChannels = m_receiver_channels->value();

                // Saving the scalars alone produced a file the application then
                // rejected for having no parameters, so the word map goes with
                // them: the one this stream loaded, or the default map for these
                // counts. Which map that is, is the Model's rule, not the dialog's.
                QString error;
                if (!FrameSetup::saveReceiverParamsFile(
                        filename, params, m_receiver_params_toml,
                        FrameSetup::wordsInMinorFrame(m_fs.bitsPerFrame->value()), error))
                {
                    QMessageBox::warning(this, tr("Save Receiver Parameters"), error);
                }
            });

            outer->addLayout(grid);
        }

        outer->addSpacing(DialogLayout::kControlGap);

        // ---- Group 3: Non-linear step calibration (US5.3) -------------------
        {
            auto* row = new QHBoxLayout;
            auto* extractBtn = new QPushButton("Apply Cal", this);
            extractBtn->setToolTip(
                "Build a non-linear calibration profile from a calibration "
                "Chapter 10 file and a step-config TOML. Uses the word map, "
                "frame sync, and polarity above plus the time channel from the "
                "main dialog. Runtime-only; the profile itself is not saved to disk.");
            connect(extractBtn, &QPushButton::clicked, this,
                    [this]() { onExtractCalibration(); });
            row->addWidget(extractBtn);

            m_calibration_label = new QLabel(this);
            row->addWidget(m_calibration_label);
            row->addStretch(1);
            outer->addLayout(row);
            updateCalibrationLabel();
        }

        addSeparator(outer, this);

        DialogButtons btns = makeDialogButtons(this, tr("OK"));
        connect(btns.primary, &QPushButton::clicked, this, [this]() {
            if (m_fs.syncPattern->text().trimmed().isEmpty())
            {
                QMessageBox::warning(this, tr("Missing Frame Sync"),
                    tr("A frame sync pattern is required (e.g. FE6B2840)."));
                return;
            }
            accept();
        });

        m_apply_to_all = new QCheckBox(this);
        m_apply_to_all->setToolTip("Copy these settings to every other selected "
                                  "stream currently set to Receiver SNR mode.");
        addBottomBar(outer, btns, this, m_apply_to_all, "Apply to all Receiver SNR streams");

        adjustSize();
    }

    QString frameSyncPattern()   const { return m_fs.syncPattern->text().trimmed().toUpper(); }
    QString frameSyncMask()      const { return m_fs.syncMask->text().trimmed().toUpper(); }
    int     bitsPerFrame()       const { return m_fs.bitsPerFrame->value(); }
    bool    randomized()         const { return m_randomized->isChecked(); }
    bool    inverted()           const { return m_inverted->isChecked(); }
    int     samplePeriodIndex()    const { return m_fs.sampleRate->currentIndex(); }
    double  dataRateMbps()       const { return m_fs.dataRate->value(); }
    /// The acquisition fields as a bundle (for building a CalibrationExtractor::Request).
    FrameSyncParams frameSyncParams() const {
        return { frameSyncPattern(), frameSyncMask(), bitsPerFrame(),
                 randomized(), inverted(), dataRateMbps() };
    }
    int     polarityIndex()      const { return m_polarity->currentIndex(); }
    int     slopeIndex()         const { return m_slope->currentIndex(); }
    double  scaleDdBPerV()       const { return m_scale->value(); }
    int     numReceivers()       const { return m_num_receivers->value(); }
    int     receiverChannels()   const { return m_receiver_channels->value(); }
    QString receiverParamsToml() const { return m_receiver_params_toml; }
    QString lastTomlDir()        const { return m_toml_dir; }
    bool    applyToAll()         const { return m_apply_to_all->isChecked(); }
    QHash<int, CalibrationProfile> calibrationByWord() const { return m_calibration_by_word; }

    // Input references: captured from the setup sub-dialog on extraction so a
    // processing template can store them alongside the config.
    QString calCh10Path()  const { return m_cal_ch10_path; }
    QString stepTomlPath() const { return m_step_toml_path; }
    double  clipStartSec() const { return m_clip_start_sec; }
    double  clipEndSec()   const { return m_clip_end_sec; }

private:
    void updateCalibrationLabel()
    {
        int n = 0;
        for (const CalibrationProfile& p : m_calibration_by_word)
        {
            if (p.valid) n++;
        }
        if (n == 0)
            m_calibration_label->setText(
                "<span style='color: gray;'>(none — using linear calibration)</span>");
        else
            m_calibration_label->setText(
                QString("%1 channel(s) calibrated (non-linear)").arg(n));
    }

    /// Opens the calibration setup dialog. The dialog processes the calibration
    /// file in full as soon as both inputs are loaded and exposes the resulting
    /// per-channel profiles; here we simply adopt them on accept (US5.3).
    void onExtractCalibration()
    {
        if (m_time_channel_id < 0)
        {
            QMessageBox::warning(this, tr("No Time Channel"),
                tr("Select a Time Channel in the Configure Streams dialog before "
                   "extracting calibration."));
            return;
        }

        CalibrationExtractor::Request base;
        base.timeChannelId      = m_time_channel_id;
        base.pcmChannelId       = m_pcm_channel_id;
        base.sync               = frameSyncParams();
        base.receiverParamsToml = m_receiver_params_toml;
        base.numReceivers       = numReceivers();
        base.receiverChannels   = receiverChannels();
        // Mirror the main run's byte order, or the extracted steps come from a
        // different bitstream than the data they will calibrate.
        base.swapBytes          = m_swap_bytes;
        CalibrationSetupDialog setup(base, m_toml_dir, m_app_root, m_step_toml_path, this);
        if (setup.exec() != QDialog::Accepted) return;
        m_toml_dir = setup.lastTomlDir();

        m_calibration_by_word = setup.calibrationByWord();
        m_cal_ch10_path  = setup.calFilePath();
        m_step_toml_path = setup.stepFilePath();
        m_clip_start_sec = setup.clipStartSec();
        m_clip_end_sec   = setup.clipEndSec();
        updateCalibrationLabel();
        const QString report = setup.calibrationReport();
        QMessageBox::information(this, tr("Calibration Extracted"),
            report.isEmpty()
                ? tr("Applied non-linear calibration to %1 channel(s).")
                      .arg(m_calibration_by_word.size())
                : report);
    }

    FrameSyncWidgets m_fs; ///< Frame Sync / Mask / Bits Per Frame / Data Rate / Average Period.
    QCheckBox*      m_randomized       = nullptr;
    QCheckBox*      m_inverted         = nullptr;
    QComboBox*      m_polarity         = nullptr;
    QComboBox*      m_slope            = nullptr;
    QDoubleSpinBox* m_scale            = nullptr;
    QSpinBox*       m_num_receivers     = nullptr;
    QSpinBox*       m_receiver_channels = nullptr;
    QCheckBox*      m_apply_to_all       = nullptr;
    QString         m_receiver_params_toml;
    QString         m_toml_dir;
    QString         m_app_root;

    // Non-linear step calibration (US5.3)
    int             m_time_channel_id = -1;     ///< Time channel ID inherited from the parent dialog.
    bool            m_swap_bytes = false;      ///< File-level byte order, inherited from the parent dialog.
    int             m_pcm_channel_id  = -1;     ///< PCM channel ID of the stream being calibrated.
    QHash<int, CalibrationProfile> m_calibration_by_word; ///< Extracted profiles, keyed by word index.
    QString         m_cal_ch10_path;   ///< Calibration input reference (serialized in a template).
    QString         m_step_toml_path;  ///< Calibration input reference (serialized in a template).
    double          m_clip_start_sec = 0.0; ///< Calibration input reference (serialized in a template).
    double          m_clip_end_sec   = 0.0; ///< Calibration input reference (serialized in a template).
    QLabel*         m_calibration_label = nullptr; ///< Status text for the calibration section.
};

} // namespace

#endif // STREAMSUBDIALOGS_H
