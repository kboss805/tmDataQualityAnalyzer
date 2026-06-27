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
        bool inv = (cfg.value("Frame/Inverted", false).toString() == "true");
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
    w.syncPattern->setText(cfg.frameSyncPattern);
    w.syncPattern->setPlaceholderText(syncPlaceholder);
    w.syncPattern->setMinimumWidth(110);
    w.syncPattern->setToolTip(syncTooltip);
    grid->addWidget(new QLabel("Frame Sync"),      0, 0, Qt::AlignLeft | Qt::AlignVCenter);
    grid->addWidget(w.syncPattern,                 1, 0, Qt::AlignLeft | Qt::AlignVCenter);

    w.syncMask = new QLineEdit(parent);
    w.syncMask->setValidator(new QRegularExpressionValidator(kHexRegex, parent));
    w.syncMask->setText(cfg.frameSyncMask);
    w.syncMask->setPlaceholderText("e.g. FFFFFFFF");
    w.syncMask->setMinimumWidth(110);
    w.syncMask->setToolTip("Hex mask applied during frame sync comparison. Set bits are compared; cleared bits are ignored.");
    grid->addWidget(new QLabel("Frame Sync Mask"), 0, 1, Qt::AlignLeft | Qt::AlignVCenter);
    grid->addWidget(w.syncMask,                    1, 1, Qt::AlignLeft | Qt::AlignVCenter);

    w.bitsPerFrame = new QSpinBox(parent);
    w.bitsPerFrame->setRange(PCMConstants::kMinFrameLengthBits,
                             PCMConstants::kMaxFrameLengthBits);
    w.bitsPerFrame->setValue(cfg.bitsInMinorFrame);
    w.bitsPerFrame->setMinimumWidth(80);
    w.bitsPerFrame->setToolTip("Total number of bits in one minor frame, including the frame sync word.");
    grid->addWidget(new QLabel("Bits Per Frame"),  0, 2, Qt::AlignLeft | Qt::AlignVCenter);
    grid->addWidget(w.bitsPerFrame,                1, 2, Qt::AlignLeft | Qt::AlignVCenter);

    // Spacer row between the frame-sync inputs and the rate/period group.
    grid->setRowMinimumHeight(2, 8);

    w.dataRate = new QDoubleSpinBox(parent);
    w.dataRate->setRange(0.0, 1000.0);
    w.dataRate->setDecimals(3);
    w.dataRate->setSingleStep(0.1);
    if (cfg.tmatsDataRateMbps > 0.0)
        w.dataRate->setSpecialValueText(QString::number(cfg.tmatsDataRateMbps, 'f', 3));
    else
        w.dataRate->setSpecialValueText("TMATS");
    w.dataRate->setValue(cfg.dataRateMbps);
    w.dataRate->setMinimumWidth(130);
    w.dataRate->setToolTip("Telemetry data rate in Mbps. Set to 0 (TMATS) to derive the rate from the file metadata.");
    grid->addWidget(new QLabel("Data Rate (Mbps)"), 3, 0, Qt::AlignLeft | Qt::AlignVCenter);
    grid->addWidget(w.dataRate,                     4, 0, Qt::AlignLeft | Qt::AlignVCenter);

    w.sampleRate = new QComboBox(parent);
    w.sampleRate->addItem(periodText(UIConstants::kSamplePeriod1s));
    w.sampleRate->addItem(periodText(UIConstants::kSamplePeriod100ms));
    w.sampleRate->addItem(periodText(UIConstants::kSamplePeriod10ms));
    w.sampleRate->setCurrentIndex(cfg.samplePeriodIndex);
    w.sampleRate->setToolTip("Integration window for statistics. Shorter periods give finer time resolution; longer periods smooth noise.");
    w.sampleRate->setFixedHeight(w.syncPattern->sizeHint().height());
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

        // buildFrameSyncRow fills cols 0-2 (FrameSync/DataRate, Mask/Period,
        // Bits); col 3 is a flexible 16px gap and cols 4-5 hold Load/Save below.
        auto* grid = new QGridLayout;
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(4);
        grid->setColumnMinimumWidth(3, 16);
        grid->setColumnStretch(3, 1);

        // Frame Sync / Mask / Bits + Data Rate / Average Period (rows 0-4).
        FrameSyncWidgets fs = buildFrameSyncRow(grid, this, cfg,
            "e.g. A345CA5C",
            "Hex frame synchronization word (e.g. A345CA5C). Used to detect frame boundaries in the PCM stream.");
        m_syncPattern  = fs.syncPattern;
        m_syncMask     = fs.syncMask;
        m_bitsPerFrame = fs.bitsPerFrame;
        m_dataRate     = fs.dataRate;
        m_sampleRate   = fs.sampleRate;

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
                this, tr("Load Frame Sync Parameters"),
                settingsSubdir(m_app_root, UIConstants::kFramesyncPatternsDirName, m_toml_dir),
                tr("TOML Files (*.toml);;All Files (*.*)"));
            if (!filename.isEmpty())
                loadFrameSyncFromToml(filename, m_syncPattern, m_syncMask,
                                      m_bitsPerFrame, m_toml_dir, m_inverted);
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
                                m_inverted->isChecked(),
                                m_toml_dir);
        });

        auto* outer = new QVBoxLayout(this);
        outer->setSpacing(4);
        outer->addLayout(grid);
        outer->addSpacing(16);

        // Derandomize toggle row
        m_randomized = new QCheckBox(this);
        m_randomized->setChecked(cfg.randomized);
        m_randomized->setToolTip("Apply RNRZ-L self-synchronizing descrambler to the data stream.");
        {
            auto* row = new QHBoxLayout;
            row->setSpacing(6);
            row->addWidget(m_randomized);
            row->addWidget(new QLabel("Derandomize", this));
            row->addStretch(1);
            outer->addLayout(row);
        }

        // Invert Data toggle row
        m_inverted = new QCheckBox(this);
        m_inverted->setChecked(cfg.inverted);
        m_inverted->setToolTip("Invert every bit of the raw data stream before processing (use when the PCM signal polarity is inverted).");
        {
            auto* row = new QHBoxLayout;
            row->setSpacing(6);
            row->addWidget(m_inverted);
            row->addWidget(new QLabel("Invert Data", this));
            row->addStretch(1);
            outer->addLayout(row);
        }

        outer->addSpacing(8);
        outer->addStretch(1);

        addSeparator(outer, this);

        DialogButtons btns = makeDialogButtons(this, tr("OK"));
        QPushButton* cancelBtn = btns.cancel;
        QPushButton* okBtn     = btns.primary;
        connect(okBtn, &QPushButton::clicked, this, [this]() {
            if (m_syncPattern->text().trimmed().isEmpty())
            {
                QMessageBox::warning(this, tr("Missing Frame Sync"),
                    tr("A frame sync pattern is required (e.g. FE6B2840)."));
                return;
            }
            accept();
        });

        // Apply to all toggle row + dialog buttons
        m_applyToAll = new QCheckBox(this);
        m_applyToAll->setToolTip("Copy these settings to every other selected "
                                  "stream currently set to Frame Sync Lock mode.");
        auto* bottomLayout = new QHBoxLayout;
        {
            bottomLayout->addWidget(m_applyToAll);
            bottomLayout->addWidget(new QLabel("Apply to all Frame Sync Lock streams", this));
        }
        bottomLayout->addStretch(1);
        bottomLayout->addWidget(cancelBtn);
        bottomLayout->addWidget(okBtn);
        outer->addSpacing(4);
        outer->addLayout(bottomLayout);

        adjustSize();
    }

    QString frameSyncPattern() const { return m_syncPattern->text().trimmed().toUpper(); }
    QString frameSyncMask()    const { return m_syncMask->text().trimmed().toUpper(); }
    int     bitsPerFrame()     const { return m_bitsPerFrame->value(); }
    bool    randomized()        const { return m_randomized->isChecked(); }
    bool    inverted()          const { return m_inverted->isChecked(); }
    int     samplePeriodIndex() const { return m_sampleRate->currentIndex(); }
    double  dataRateMbps()      const { return m_dataRate->value(); }
    QString lastTomlDir()       const { return m_toml_dir; }
    bool    applyToAll()        const { return m_applyToAll->isChecked(); }

private:
    QLineEdit*      m_syncPattern  = nullptr;
    QLineEdit*      m_syncMask     = nullptr;
    QSpinBox*       m_bitsPerFrame = nullptr;
    QCheckBox*      m_randomized   = nullptr;
    QCheckBox*      m_inverted     = nullptr;
    QDoubleSpinBox* m_dataRate     = nullptr;
    QComboBox*      m_sampleRate   = nullptr;
    QCheckBox*      m_applyToAll   = nullptr;
    QString         m_toml_dir;
    QString         m_app_root;
};

////////////////////////////////////////////////////////////////////////////////
//                      CALIBRATION SETUP DIALOG (US3.2)                       //
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
    CalibrationSetupDialog(const QString& frameSyncHex,
                           const QString& frameSyncMaskHex,
                           int bitsInMinorFrame,
                           bool randomized,
                           bool inverted,
                           double dataRateMbps,
                           int timeChannelId,
                           int pcmChannelId,
                           const QString& receiverParamsToml,
                           int numReceivers,
                           int receiverChannels,
                           const QString& tomlDir,
                           const QString& appRoot,
                           QWidget* parent = nullptr)
        : QDialog(parent)
        , m_frameSyncHex(frameSyncHex)
        , m_frameSyncMaskHex(frameSyncMaskHex)
        , m_bitsInMinorFrame(bitsInMinorFrame)
        , m_randomized(randomized)
        , m_inverted(inverted)
        , m_dataRateMbps(dataRateMbps)
        , m_timeChannelId(timeChannelId)
        , m_pcmChannelId(pcmChannelId)
        , m_receiverParamsToml(receiverParamsToml)
        , m_numReceivers(numReceivers)
        , m_receiverChannels(receiverChannels)
        , m_tomlDir(tomlDir)
        , m_appRoot(appRoot)
    {
        setWindowTitle("Extract Calibration");
        setModal(true);

        auto* outer = new QVBoxLayout(this);
        outer->setSpacing(8);

        auto* grid = new QGridLayout;
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(4);
        grid->setColumnStretch(1, 1);

        // ---- Row 0/1: Step Cal file -----------------------------------------
        grid->addWidget(new QLabel("Step Cal File:"), 0, 0);
        m_stepPathLabel = new QLabel(this);
        m_stepPathLabel->setMinimumWidth(260);
        grid->addWidget(m_stepPathLabel, 0, 1);
        auto* stepBrowse = new QPushButton("Browse...", this);
        stepBrowse->setToolTip("Browse for the step calibration configuration TOML file that defines the RF signal step levels.");
        grid->addWidget(stepBrowse, 0, 2);
        m_stepStatus = new QLabel(this);
        grid->addWidget(m_stepStatus, 1, 1, 1, 2);

        // ---- Row 2/3: Calibration Ch10 file ---------------------------------
        grid->setRowMinimumHeight(2, 8);
        grid->addWidget(new QLabel("Calibration Ch10:"), 3, 0);
        m_calPathLabel = new QLabel(this);
        m_calPathLabel->setMinimumWidth(260);
        grid->addWidget(m_calPathLabel, 3, 1);
        m_calBrowse = new QPushButton("Browse...", this);
        m_calBrowse->setToolTip("Browse for the calibration Chapter 10 recording file. The last folder you used will be remembered.");
        grid->addWidget(m_calBrowse, 3, 2);
        m_calStatus = new QLabel(this);
        grid->addWidget(m_calStatus, 4, 1, 1, 2);

        // ---- Rows 5-7: Clip Start / Clip End (seconds), stacked ----------------
        // Ignore the first/last N seconds of the cal recording before detecting
        // steps, so signal-generator turn-on transients (or trailing junk) don't
        // get mistaken for calibration plateaus.
        {
            // 16 px breathing room between the cal-file status and the clip controls.
            grid->addItem(new QSpacerItem(0, 16, QSizePolicy::Minimum, QSizePolicy::Fixed), 5, 0);

            grid->addWidget(new QLabel("Clip Start (s):", this), 6, 0);
            m_clipStart = new QDoubleSpinBox(this);
            m_clipStart->setRange(0.0, 100000.0);
            m_clipStart->setDecimals(1);
            m_clipStart->setSingleStep(1.0);
            m_clipStart->setValue(0.0);
            m_clipStart->setFixedWidth(100);
            m_clipStart->setToolTip("Ignore this many seconds at the START of the "
                "calibration recording before detecting steps (skips signal-generator "
                "turn-on transients).");
            grid->addWidget(m_clipStart, 6, 1, Qt::AlignLeft);

            grid->addWidget(new QLabel("Clip End (s):", this), 7, 0);
            m_clipEnd = new QDoubleSpinBox(this);
            m_clipEnd->setRange(0.0, 100000.0);
            m_clipEnd->setDecimals(1);
            m_clipEnd->setSingleStep(1.0);
            m_clipEnd->setValue(0.0);
            m_clipEnd->setFixedWidth(100);
            m_clipEnd->setToolTip("Ignore this many seconds at the END of the "
                "calibration recording before detecting steps.");
            grid->addWidget(m_clipEnd, 7, 1, Qt::AlignLeft);

            // Re-run extraction when the user changes a clip value (only fires if
            // both files are already loaded).
            connect(m_clipStart, &QDoubleSpinBox::editingFinished,
                    this, [this]() { maybeRunExtraction(); });
            connect(m_clipEnd, &QDoubleSpinBox::editingFinished,
                    this, [this]() { maybeRunExtraction(); });
        }

        outer->addLayout(grid);

        connect(stepBrowse,  &QPushButton::clicked, this, [this]() { onBrowseStep(); });
        connect(m_calBrowse, &QPushButton::clicked, this, [this]() { onBrowseCal(); });

        addSeparator(outer, this);

        DialogButtons calBtns = makeDialogButtons(this, tr("OK"));
        m_okButton = calBtns.primary;
        connect(m_okButton, &QPushButton::clicked, this, &QDialog::accept);

        auto* calBtnLayout = new QHBoxLayout;
        calBtnLayout->addStretch(1);
        calBtnLayout->addWidget(calBtns.cancel);
        calBtnLayout->addWidget(m_okButton);
        outer->addLayout(calBtnLayout);

        setStatus(m_stepStatus, Pending, "No file selected.");
        setStatus(m_calStatus,  Pending, "No file selected.");
        updateOk();
        adjustSize();
    }

    /// Per-channel calibration profiles produced by the whole-file extraction,
    /// keyed by word index. Valid after the dialog is accepted.
    QHash<int, CalibrationProfile> calibrationByWord() const { return m_calibrationByWord; }
    QString lastTomlDir() const { return m_tomlDir; }

private:
    enum StatusKind { Pending, Ok, Fail };

    void setStatus(QLabel* label, StatusKind kind, const QString& text)
    {
        QString prefix;
        QString color;
        switch (kind)
        {
            case Ok:      prefix = "✓ "; color = "green"; break;  // checkmark
            case Fail:    prefix = "✗ "; color = "#cc0000"; break; // cross
            case Pending: default: color = "gray"; break;
        }
        label->setText(QString("<span style='color: %1;'>%2%3</span>")
                           .arg(color, prefix, text.toHtmlEscaped()));
    }

    void updateOk()
    {
        if (m_okButton != nullptr)
        {
            m_okButton->setEnabled(m_stepOk && m_calOk);
        }
    }

    void onBrowseStep()
    {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Select Step Configuration (TOML)"),
            settingsSubdir(m_appRoot, UIConstants::kRcvrCalsDirName, m_tomlDir),
            tr("TOML Files (*.toml);;All Files (*.*)"));
        if (path.isEmpty()) return;

        m_tomlDir = QFileInfo(path).absolutePath();
        m_stepPath = path;
        m_stepPathLabel->setText(QFileInfo(path).fileName());

        QVector<StepDefinition> steps;
        QString error;
        if (StepDetector::parseStepConfig(path, steps, error))
        {
            m_steps  = steps;
            m_stepOk = true;
            setStatus(m_stepStatus, Ok,
                      QString("%1 steps parsed.").arg(steps.size()));
        }
        else
        {
            m_steps.clear();
            m_stepOk = false;
            setStatus(m_stepStatus, Fail, error);
        }
        updateOk();
        maybeRunExtraction();
    }

    void onBrowseCal()
    {
        if (m_frameSyncHex.trimmed().isEmpty())
        {
            QMessageBox::warning(this, tr("Missing Frame Sync"),
                tr("Set a frame sync pattern before loading the calibration file."));
            return;
        }

        QSettings settings;
        const QString lastCalDir = settings.value("CalibrationSetupDialog/lastCalDir").toString();

        const QString path = QFileDialog::getOpenFileName(
            this, tr("Select Calibration Chapter 10 File"),
            lastCalDir.isEmpty() ? m_tomlDir : lastCalDir,
            tr("Chapter 10 Files (*.ch10 *.c10);;All Files (*.*)"));
        if (path.isEmpty()) return;

        settings.setValue("CalibrationSetupDialog/lastCalDir", QFileInfo(path).absolutePath());

        m_calPath = path;
        m_calPathLabel->setText(QFileInfo(path).fileName());
        maybeRunExtraction();
    }

    /// Runs the whole-file calibration extraction once both inputs are present.
    /// This is the single processing pass: it detects the step plateaus and
    /// builds the per-channel profiles. OK simply hands these back to the owner.
    void maybeRunExtraction()
    {
        if (m_calPath.isEmpty())
        {
            return; // Nothing to do until a cal file is chosen.
        }
        if (!m_stepOk)
        {
            m_calOk = false;
            setStatus(m_calStatus, Pending,
                      tr("Select a valid step cal file to process this recording."));
            updateOk();
            return;
        }

        m_calOk = false;
        m_calibrationByWord.clear();
        setStatus(m_calStatus, Pending, tr("Processing calibration file…"));
        updateOk();

        CalibrationExtractor::Request req;
        req.calFilename        = m_calPath;
        req.timeChannelId      = m_timeChannelId;
        req.pcmChannelId       = m_pcmChannelId;
        req.frameSyncHex       = m_frameSyncHex;
        req.frameSyncMaskHex   = m_frameSyncMaskHex;
        req.bitsInMinorFrame   = m_bitsInMinorFrame;
        req.randomized         = m_randomized;
        req.inverted           = m_inverted;
        req.dataRateMbps       = m_dataRateMbps;
        // Resolve the word map the SAME way the main processing run does
        // (mainviewmodel buildJob): when the user hasn't picked an explicit
        // Receiver Parameters file, fall back to the shipped default.toml rather
        // than letting the extractor synthesize a sequential grid. The two passes
        // MUST agree on word indices, because the resulting profiles are matched
        // to plot channels by word — a divergent map silently misattaches every
        // profile and the plot falls back to linear calibration.
        req.receiverParamsToml = m_receiverParamsToml;
        if (req.receiverParamsToml.isEmpty())
        {
            const QString defaultRcvrParams = m_appRoot + "/" + UIConstants::kSettingsDirName +
                "/" + UIConstants::kReceiverParamsDirName + "/" + UIConstants::kDefaultTomlFilename;
            if (QFileInfo::exists(defaultRcvrParams))
            {
                req.receiverParamsToml = defaultRcvrParams;
            }
        }
        req.numReceivers       = m_numReceivers;
        req.receiverChannels   = m_receiverChannels;
        req.steps              = m_steps;
        req.clipStartSec       = m_clipStart ? m_clipStart->value() : 0.0;
        req.clipEndSec         = m_clipEnd ? m_clipEnd->value() : 0.0;

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
            setStatus(m_calStatus, Fail, summary);
            updateOk();
            return;
        }

        int valid = 0;
        for (const CalibrationChannelResult& r : extractor.results())
        {
            if (r.profile.valid)
            {
                m_calibrationByWord.insert(r.word, r.profile);
                valid++;
            }
        }

        m_calOk = (valid > 0);
        setStatus(m_calStatus, m_calOk ? Ok : Fail, summary);
        updateOk();
    }

    // Frame sync + receiver settings forwarded to the extractor.
    QString m_frameSyncHex;
    QString m_frameSyncMaskHex;
    int     m_bitsInMinorFrame = 0;
    bool    m_randomized       = false;
    bool    m_inverted         = false;
    double  m_dataRateMbps     = 0.0;
    int     m_timeChannelId    = -1;
    int     m_pcmChannelId     = -1;
    QString m_receiverParamsToml;
    int     m_numReceivers     = 0;
    int     m_receiverChannels = 0;
    QString m_tomlDir;
    QString m_appRoot;

    // Selected files + parsed steps.
    QString m_stepPath;
    QString m_calPath;
    QVector<StepDefinition> m_steps;
    bool    m_stepOk = false;
    bool    m_calOk  = false;

    // Extracted profiles, keyed by word index (populated by maybeRunExtraction).
    QHash<int, CalibrationProfile> m_calibrationByWord;

    // Widgets.
    QLabel*      m_stepPathLabel = nullptr;
    QLabel*      m_stepStatus    = nullptr;
    QLabel*      m_calPathLabel  = nullptr;
    QLabel*      m_calStatus     = nullptr;
    QPushButton* m_calBrowse     = nullptr;
    QPushButton* m_okButton      = nullptr;
    QDoubleSpinBox* m_clipStart  = nullptr; ///< Seconds to clip from the start before step detection.
    QDoubleSpinBox* m_clipEnd    = nullptr; ///< Seconds to clip from the end before step detection.
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
                               QWidget* parent = nullptr)
        : QDialog(parent)
        , m_receiverParamsToml(cfg.receiverParamsToml)
        , m_toml_dir(toml_dir)
        , m_app_root(app_root)
        , m_timeChannelId(time_channel_id)
        , m_pcmChannelId(cfg.pcmChannelId)
        , m_calibrationByWord(cfg.calibrationByWord)
    {
        setWindowTitle("Configure Receiver SNR — " + cfg.label);
        setModal(true);

        auto* outer = new QVBoxLayout(this);
        outer->setSpacing(6);

        // ---- Group 1: Frame sync + acquisition settings --------------------
        {
            auto* grid = new QGridLayout;
            grid->setHorizontalSpacing(10);
            grid->setVerticalSpacing(4);

            // Frame Sync / Mask / Bits + Data Rate / Average Period (rows 0-4).
            FrameSyncWidgets fs = buildFrameSyncRow(grid, this, cfg,
                "e.g. FE6B2840",
                "Hex frame synchronization word (e.g. FE6B2840). Used to detect frame boundaries in the receiver PCM stream.");
            m_syncPattern  = fs.syncPattern;
            m_syncMask     = fs.syncMask;
            m_bitsPerFrame = fs.bitsPerFrame;
            m_dataRate     = fs.dataRate;
            m_sampleRate   = fs.sampleRate;

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
                    this, tr("Load Frame Sync Parameters"),
                    settingsSubdir(m_app_root, UIConstants::kFramesyncPatternsDirName, m_toml_dir),
                    tr("TOML Files (*.toml);;All Files (*.*)"));
                if (!filename.isEmpty())
                    loadFrameSyncFromToml(filename, m_syncPattern, m_syncMask,
                                         m_bitsPerFrame, m_toml_dir, m_inverted);
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
                                    m_inverted->isChecked(),
                                    m_toml_dir);
            });

            outer->addLayout(grid);
        }

        outer->addSpacing(16);

        // Derandomize toggle row
        m_randomized = new QCheckBox(this);
        m_randomized->setChecked(cfg.randomized);
        m_randomized->setToolTip("Apply RNRZ-L self-synchronizing descrambler to the data stream.");
        {
            auto* row = new QHBoxLayout;
            row->setSpacing(6);
            row->addWidget(m_randomized);
            row->addWidget(new QLabel("Derandomize", this));
            row->addStretch(1);
            outer->addLayout(row);
        }

        // Invert Data toggle row
        m_inverted = new QCheckBox(this);
        m_inverted->setChecked(cfg.inverted);
        m_inverted->setToolTip("Invert every bit of the raw data stream before processing (use when the PCM signal polarity is inverted).");
        {
            auto* row = new QHBoxLayout;
            row->setSpacing(6);
            row->addWidget(m_inverted);
            row->addWidget(new QLabel("Invert Data", this));
            row->addStretch(1);
            outer->addLayout(row);
        }

        outer->addSpacing(8);

        // ---- Separator ------------------------------------------------------
        addSeparator(outer, this);

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
            m_polarity->setToolTip("ADC output polarity. Positive = high voltage maps to highest dB; Negative = inverted.");
            m_polarity->setFixedHeight(m_syncPattern->sizeHint().height());
            grid->addWidget(m_polarity, 1, 0, Qt::AlignHCenter);

            m_slope = new QComboBox(this);
            m_slope->addItem("±10 V");
            m_slope->addItem("±5 V");
            m_slope->addItem("0–10 V");
            m_slope->addItem("0–5 V");
            m_slope->setCurrentIndex(cfg.slopeIndex);
            m_slope->setToolTip("ADC voltage range. Select the range that matches your receiver's analog output voltage span.");
            m_slope->setFixedHeight(m_syncPattern->sizeHint().height());
            grid->addWidget(m_slope, 1, 1, Qt::AlignHCenter);

            m_scale = new QDoubleSpinBox(this);
            m_scale->setRange(0.001, 999.999);
            m_scale->setDecimals(3);
            m_scale->setValue(cfg.scaleDdBPerV);
            m_scale->setMinimumWidth(100);
            m_scale->setToolTip("Calibration scale factor in dB/V. Maps the ADC voltage span to the receiver SNR range in dB.");
            grid->addWidget(m_scale, 1, 2, Qt::AlignHCenter);

            // Row 2: spacer
            grid->setRowMinimumHeight(2, 8);

            // Row 3: labels
            grid->addWidget(new QLabel("Num Rcvrs"),    3, 0, Qt::AlignHCenter);
            grid->addWidget(new QLabel("Num Channels"), 3, 1, Qt::AlignHCenter);

            // Row 4: inputs + buttons
            m_numReceivers = new QSpinBox(this);
            m_numReceivers->setRange(1, 100);
            m_numReceivers->setValue(cfg.numReceivers);
            m_numReceivers->setToolTip("Number of individual receiver units contributing channels to this stream.");
            grid->addWidget(m_numReceivers, 4, 0, Qt::AlignHCenter);

            m_receiverChannels = new QSpinBox(this);
            m_receiverChannels->setRange(1, 100);
            m_receiverChannels->setValue(cfg.receiverChannels);
            m_receiverChannels->setToolTip("Number of channels per receiver (e.g. 3 for L/R/C configuration).");
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
                    this, tr("Load Receiver Parameters"),
                    settingsSubdir(m_app_root, UIConstants::kReceiverParamsDirName, m_toml_dir),
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
        }

        outer->addSpacing(8);

        // ---- Group 3: Non-linear step calibration (US3.2) -------------------
        {
            auto* row = new QHBoxLayout;
            auto* extractBtn = new QPushButton("Apply Cal", this);
            extractBtn->setToolTip(
                "Build a non-linear calibration profile from a calibration "
                "Chapter 10 file and a step-config TOML. Uses the word map, "
                "frame sync, and polarity above plus the time channel from the "
                "main dialog. Session-only; not saved to disk.");
            connect(extractBtn, &QPushButton::clicked, this,
                    [this]() { onExtractCalibration(); });
            row->addWidget(extractBtn);

            m_calibrationLabel = new QLabel(this);
            row->addWidget(m_calibrationLabel);
            row->addStretch(1);
            outer->addLayout(row);
            updateCalibrationLabel();
        }

        addSeparator(outer, this);

        DialogButtons btns = makeDialogButtons(this, tr("OK"));
        QPushButton* cancelBtn = btns.cancel;
        QPushButton* okBtn     = btns.primary;
        connect(okBtn, &QPushButton::clicked, this, [this]() {
            if (m_syncPattern->text().trimmed().isEmpty())
            {
                QMessageBox::warning(this, tr("Missing Frame Sync"),
                    tr("A frame sync pattern is required (e.g. FE6B2840)."));
                return;
            }
            accept();
        });

        // Apply to all toggle row + dialog buttons
        m_applyToAll = new QCheckBox(this);
        m_applyToAll->setToolTip("Copy these settings to every other selected "
                                  "stream currently set to Receiver SNR mode.");
        auto* bottomLayout = new QHBoxLayout;
        {
            bottomLayout->addWidget(m_applyToAll);
            bottomLayout->addWidget(new QLabel("Apply to all Receiver SNR streams", this));
        }
        bottomLayout->addStretch(1);
        bottomLayout->addWidget(cancelBtn);
        bottomLayout->addWidget(okBtn);
        outer->addSpacing(4);
        outer->addLayout(bottomLayout);

        adjustSize();
    }

    QString frameSyncPattern()   const { return m_syncPattern->text().trimmed().toUpper(); }
    QString frameSyncMask()      const { return m_syncMask->text().trimmed().toUpper(); }
    int     bitsPerFrame()       const { return m_bitsPerFrame->value(); }
    bool    randomized()         const { return m_randomized->isChecked(); }
    bool    inverted()           const { return m_inverted->isChecked(); }
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
    QHash<int, CalibrationProfile> calibrationByWord() const { return m_calibrationByWord; }

private:
    void updateCalibrationLabel()
    {
        int n = 0;
        for (const CalibrationProfile& p : m_calibrationByWord)
        {
            if (p.valid) n++;
        }
        if (n == 0)
            m_calibrationLabel->setText(
                "<span style='color: gray;'>(none — using linear calibration)</span>");
        else
            m_calibrationLabel->setText(
                QString("%1 channel(s) calibrated (non-linear)").arg(n));
    }

    /// Opens the calibration setup dialog. The dialog processes the calibration
    /// file in full as soon as both inputs are loaded and exposes the resulting
    /// per-channel profiles; here we simply adopt them on accept (US3.2).
    void onExtractCalibration()
    {
        if (m_timeChannelId < 0)
        {
            QMessageBox::warning(this, tr("No Time Channel"),
                tr("Select a Time Channel in the Configure Streams dialog before "
                   "extracting calibration."));
            return;
        }

        CalibrationSetupDialog setup(frameSyncPattern(), frameSyncMask(),
                                     bitsPerFrame(), randomized(), inverted(), dataRateMbps(),
                                     m_timeChannelId, m_pcmChannelId,
                                     m_receiverParamsToml, numReceivers(),
                                     receiverChannels(), m_toml_dir, m_app_root, this);
        if (setup.exec() != QDialog::Accepted) return;
        m_toml_dir = setup.lastTomlDir();

        m_calibrationByWord = setup.calibrationByWord();
        updateCalibrationLabel();
        QMessageBox::information(this, tr("Calibration Extracted"),
            tr("Applied non-linear calibration to %1 channel(s).")
                .arg(m_calibrationByWord.size()));
    }

    QLineEdit*      m_syncPattern      = nullptr;
    QLineEdit*      m_syncMask         = nullptr;
    QSpinBox*       m_bitsPerFrame     = nullptr;
    QCheckBox*      m_randomized       = nullptr;
    QCheckBox*      m_inverted         = nullptr;
    QDoubleSpinBox* m_dataRate         = nullptr;
    QComboBox*      m_sampleRate       = nullptr;
    QComboBox*      m_polarity         = nullptr;
    QComboBox*      m_slope            = nullptr;
    QDoubleSpinBox* m_scale            = nullptr;
    QSpinBox*       m_numReceivers     = nullptr;
    QSpinBox*       m_receiverChannels = nullptr;
    QCheckBox*      m_applyToAll       = nullptr;
    QString         m_receiverParamsToml;
    QString         m_toml_dir;
    QString         m_app_root;

    // Non-linear step calibration (US3.2)
    int             m_timeChannelId = -1;     ///< Time channel ID inherited from the parent dialog.
    int             m_pcmChannelId  = -1;     ///< PCM channel ID of the stream being calibrated.
    QHash<int, CalibrationProfile> m_calibrationByWord; ///< Extracted profiles, keyed by word index.
    QLabel*         m_calibrationLabel = nullptr; ///< Status text for the calibration section.
};

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
        constexpr int kColWidthProcess = 48;
        constexpr int kColWidthChannel = 200;
        constexpr int kColWidthMode    = 200;
        constexpr int kColWidthSetup   = 64;
        constexpr int kColWidthReady   = 64;

        auto* header = new QWidget(this);
        auto* hl = new QHBoxLayout(header);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(0);

        auto makeHdr = [&](const QString& text, int width) {
            auto* lbl = new QLabel(text, header);
            lbl->setFixedWidth(width);
            lbl->setAlignment(Qt::AlignCenter | Qt::AlignVCenter);
            lbl->setStyleSheet("color: rgba(255,255,255,0.6);");
            hl->addWidget(lbl);
        };
        makeHdr("Process",   kColWidthProcess);
        makeHdr("Channel",   kColWidthChannel);
        makeHdr("Mode",      kColWidthMode);
        makeHdr("Configure", kColWidthSetup);
        makeHdr("Ready",     kColWidthReady);
        hl->addStretch(1);

        layout->addWidget(header);

        // Separator line beneath headers
        auto* line = new QFrame(this);
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Plain);
        line->setStyleSheet("color: rgba(255,255,255,0.15);");
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
    constexpr int kRowHeight    = 52;
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

    auto* mainBtnLayout = new QHBoxLayout;
    mainBtnLayout->addWidget(m_all_toggle);
    mainBtnLayout->addStretch(1);
    mainBtnLayout->addWidget(btns.cancel);
    mainBtnLayout->addWidget(m_ok_btn);
    layout->addLayout(mainBtnLayout);

    // m_ok_btn was nullptr during buildTable(), so call once now to reflect actual state.
    updateOkButton();
    adjustSize();
}

void StreamConfigDialog::buildTable()
{
    constexpr int kColWidthProcess = 48;
    constexpr int kColWidthChannel = 200;
    constexpr int kColWidthMode    = 200;
    constexpr int kColWidthSetup   = 64;
    constexpr int kColWidthReady   = 64;
    constexpr int kRowHeight       = 52;

    auto* containerLayout = qobject_cast<QVBoxLayout*>(m_stream_container->layout());

    m_rows.resize(static_cast<int>(m_configs.size()));

    for (int row = 0; row < m_configs.size(); row++)
    {
        const StreamConfig& cfg = m_configs[row];
        RowWidgets& w = m_rows[row];

        // Seed stored values from the incoming config
        w.frameSyncPattern   = cfg.frameSyncPattern;
        w.frameSyncMask      = cfg.frameSyncMask;
        w.bitsInFrame        = cfg.bitsInMinorFrame;
        w.randomized         = cfg.randomized;
        w.inverted           = cfg.inverted;
        w.samplePeriodIndex  = cfg.samplePeriodIndex;
        w.dataRateMbps       = cfg.dataRateMbps;
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

        // Process toggle (left-justified in fixed-width cell)
        w.process = new QCheckBox(rowWidget);
        w.process->setChecked(cfg.process);
        {
            auto* cell = new QWidget(rowWidget);
            cell->setFixedWidth(kColWidthProcess);
            cell->setAutoFillBackground(false);
            auto* cl = new QHBoxLayout(cell);
            cl->setContentsMargins(0, 0, 0, 0);
            cl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            cl->addWidget(w.process);
            hl->addWidget(cell);
        }

        // Channel label (elides if too long, tooltip shows full name)
        {
            auto* lbl = new QLabel(cfg.label, rowWidget);
            lbl->setFixedWidth(kColWidthChannel);
            lbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
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
        styleIconButton(w.gearBtn, 28);
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
    temp.inverted           = w.inverted;
    temp.samplePeriodIndex    = w.samplePeriodIndex;
    temp.dataRateMbps       = w.dataRateMbps;
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
            temp.frameSyncMask     = PCMConstants::kDefaultFrameSyncMask;
            temp.frameSyncPattern  = PCMConstants::kDefaultFrameSyncLockPattern;
            temp.bitsInMinorFrame  = PCMConstants::kDefaultFrameSyncLockBits;
            temp.receiverParamsToml.clear();
        }
        else
        {
            // Receiver SNR defaults are loaded from framesync_rcvr_default.toml (not
            // hard-coded) so the user can change the receiver frame sync.
            const ReceiverFrameDefaults d = loadReceiverFrameDefaults(m_app_root);
            temp.frameSyncPattern = d.pattern;
            temp.frameSyncMask    = d.mask;
            temp.bitsInMinorFrame = d.bits;
        }
    }

    if (is_frame_sync_lock)
    {
        FrameLockSetupDialog dlg(temp, m_toml_dir, m_app_root, this);
        if (dlg.exec() == QDialog::Accepted)
        {
            w.frameSyncPattern   = dlg.frameSyncPattern();
            w.frameSyncMask      = dlg.frameSyncMask();
            w.bitsInFrame        = dlg.bitsPerFrame();
            w.randomized         = dlg.randomized();
            w.inverted           = dlg.inverted();
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
                    rw.inverted           = dlg.inverted();
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
        ReceiverSNRDialog dlg(temp, m_toml_dir, m_time_channel_id, m_app_root, this);
        if (dlg.exec() == QDialog::Accepted)
        {
            w.frameSyncPattern   = dlg.frameSyncPattern();
            w.frameSyncMask      = dlg.frameSyncMask();
            w.bitsInFrame        = dlg.bitsPerFrame();
            w.randomized         = dlg.randomized();
            w.inverted           = dlg.inverted();
            w.samplePeriodIndex    = dlg.samplePeriodIndex();
            w.dataRateMbps       = dlg.dataRateMbps();
            w.polarityIndex      = dlg.polarityIndex();
            w.slopeIndex         = dlg.slopeIndex();
            w.scaleDdBPerV       = dlg.scaleDdBPerV();
            w.numReceivers       = dlg.numReceivers();
            w.receiverChannels   = dlg.receiverChannels();
            w.receiverParamsToml = dlg.receiverParamsToml();
            w.calibrationByWord  = dlg.calibrationByWord();
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
        result[row].frameSyncPattern  = w.frameSyncPattern;
        result[row].frameSyncMask     = w.frameSyncMask;
        result[row].bitsInMinorFrame  = w.bitsInFrame;
        result[row].randomized        = w.randomized;
        result[row].inverted          = w.inverted;
        result[row].samplePeriodIndex   = w.samplePeriodIndex;
        result[row].dataRateMbps      = w.dataRateMbps;
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
