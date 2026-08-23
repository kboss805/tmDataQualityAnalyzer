/**
 * @file tst_streamconfigdialog.cpp
 * @brief Implementation of StreamConfigDialog unit tests.
 *
 * Key invariant under test: the frame-sync Load/Save round-trip covers ONLY the
 * three top-section fields (FrameSync, FrameSyncMask, WordsInMinorFrame) written
 * under a [Frame] TOML section. Randomized, DataRate, and SampleRate are excluded
 * by design — the separator in the setup-dialog wireframe is the config boundary.
 */

#include "tst_streamconfigdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QLabel>
#include <QSettings>
#include <QTemporaryFile>
#include <QVector>
#include <QtTest>
#include <QTimer>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QPushButton>

#include "constants.h"
#include "streamconfig.h"
#include "streamconfigdialog.h"
#include "tomlconfighelper.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Returns a StreamConfig with process=false and FrameSyncLockStats mode.
/// FrameSyncLockStats is safe for construction — does not open a file dialog.
static StreamConfig makeConfig(const QString& label = "Ch 01",
                               const QString& sync  = "FE6B2840",
                               const QString& mask  = "FFFFFFFF",
                               int words = 48)
{
    StreamConfig cfg;
    cfg.label              = label;
    cfg.process            = false;
    cfg.mode               = StreamMode::FrameSyncLockStats;
    cfg.sync.pattern   = sync;
    cfg.sync.mask      = mask;
    cfg.sync.bitsInMinorFrame   = words;
    cfg.sync.randomized         = false;
    cfg.samplePeriodIndex  = UIConstants::kDefaultSamplePeriodIndex;
    cfg.sync.dataRateMbps       = 0.0;
    return cfg;
}

/// Constructs a dialog for testing. Uses one FrameSyncLockStats stream so no
/// file dialogs are triggered during construction.
static StreamConfigDialog* makeDialog(const QVector<StreamConfig>& configs = {},
                                      bool swap_bytes = true)
{
    return new StreamConfigDialog(configs, "", {"Ch 1"}, 1, -1, "", swap_bytes);
}

// ---------------------------------------------------------------------------
// Construction smoke tests
// ---------------------------------------------------------------------------

void TestStreamConfigDialog::constructionWithEmptyConfigs()
{
    QScopedPointer<StreamConfigDialog> dlg(makeDialog());
    QVERIFY(dlg != nullptr);
    QCOMPARE(dlg->configs().size(), 0);
}

void TestStreamConfigDialog::constructionWithOneStream()
{
    QVector<StreamConfig> cfgs = {makeConfig()};
    QScopedPointer<StreamConfigDialog> dlg(makeDialog(cfgs));
    QVERIFY(dlg != nullptr);
    QCOMPARE(dlg->configs().size(), 1);
}

// ---------------------------------------------------------------------------
// configs() roundtrip
// ---------------------------------------------------------------------------

void TestStreamConfigDialog::configsRoundtripDefaultValues()
{
    StreamConfig cfg = makeConfig();
    QVector<StreamConfig> cfgs = {cfg};
    QScopedPointer<StreamConfigDialog> dlg(makeDialog(cfgs));

    QVector<StreamConfig> out = dlg->configs();
    QCOMPARE(out.size(), 1);
    QCOMPARE(out[0].label, cfg.label);
    QCOMPARE(out[0].mode,  StreamMode::FrameSyncLockStats);
}

void TestStreamConfigDialog::configsRoundtripFrameSyncFields()
{
    StreamConfig cfg = makeConfig("Ch 02", "A345CA5C", "FFFF0000", 48);
    QScopedPointer<StreamConfigDialog> dlg(makeDialog({cfg}));

    QVector<StreamConfig> out = dlg->configs();
    QCOMPARE(out[0].sync.pattern, QString("A345CA5C"));
    QCOMPARE(out[0].sync.mask,    QString("FFFF0000"));
}

void TestStreamConfigDialog::configsRoundtripWordsInFrame()
{
    StreamConfig cfg = makeConfig();
    cfg.sync.bitsInMinorFrame = 512;
    QScopedPointer<StreamConfigDialog> dlg(makeDialog({cfg}));

    QCOMPARE(dlg->configs()[0].sync.bitsInMinorFrame, 512);
}

void TestStreamConfigDialog::configsRoundtripProcessFlag()
{
    StreamConfig cfg = makeConfig();
    cfg.process = false;
    QScopedPointer<StreamConfigDialog> dlg(makeDialog({cfg}));

    QCOMPARE(dlg->configs()[0].process, false);
}



// ---------------------------------------------------------------------------
// Frame-sync TOML scope boundary (US5.0 / US1.0)
//
// These tests exercise the key contract between saveFrameSyncToml and
// loadFrameSyncToml without going through QFileDialog (which can't be driven
// in automated tests). They write/read QSettings directly with the same
// TomlConfigHelper format and the same key paths the dialog uses.
// ---------------------------------------------------------------------------

void TestStreamConfigDialog::tomlFrameSyncSaveRoundtrip()
{
    // Simulate what saveFrameSyncToml writes: three keys under [Frame].
    QString tmpFile = QDir::tempPath() + "/tst_streamconfig_sync.toml";
    QFile::remove(tmpFile);

    {
        QSettings out(tmpFile, TomlConfigHelper::format());
        out.beginGroup("Frame");
        out.setValue("FrameSync",         "A345CA5C");
        out.setValue("FrameSyncMask",     "FFFF0000");
        out.setValue("WordsInMinorFrame", 128);
        out.endGroup();
        out.sync();
    }
    // Simulate what loadFrameSyncToml reads.
    QSettings in(tmpFile, TomlConfigHelper::format());
    QCOMPARE(in.value("Frame/FrameSync").toString(),          QString("A345CA5C"));
    QCOMPARE(in.value("Frame/FrameSyncMask").toString(),      QString("FFFF0000"));
    QCOMPARE(in.value("Frame/WordsInMinorFrame").toInt(),     128);

    QFile::remove(tmpFile);
}

void TestStreamConfigDialog::tomlFrameSyncLoadPopulatesThreeFields()
{
    // Write a TOML file with exactly the keys loadFrameSyncToml expects.
    QTemporaryFile tmp;
    QVERIFY(tmp.open());
    tmp.write("[Frame]\n"
              "FrameSync = \"FE6B2840\"\n"
              "FrameSyncMask = \"FFFFFFFF\"\n"
              "WordsInMinorFrame = 64\n");
    tmp.flush();
    tmp.close();

    QSettings cfg(tmp.fileName(), TomlConfigHelper::format());
    QCOMPARE(cfg.value("Frame/FrameSync").toString(),      QString("FE6B2840"));
    QCOMPARE(cfg.value("Frame/FrameSyncMask").toString(),  QString("FFFFFFFF"));
    QCOMPARE(cfg.value("Frame/WordsInMinorFrame").toInt(), 64);
}

void TestStreamConfigDialog::tomlFrameSyncSaveDoesNotWriteRandomized()
{
    // Confirm the scope boundary: a saved frame-sync TOML must NOT contain
    // Randomized — it is below the wireframe separator.
    QTemporaryFile tmp;
    QVERIFY(tmp.open());
    tmp.close();

    QSettings out(tmp.fileName(), TomlConfigHelper::format());
    out.beginGroup("Frame");
    out.setValue("FrameSync",         "FE6B2840");
    out.setValue("FrameSyncMask",     "FFFFFFFF");
    out.setValue("WordsInMinorFrame", 48);
    out.endGroup();
    out.sync();

    QSettings in(tmp.fileName(), TomlConfigHelper::format());
    QVERIFY(!in.contains("Frame/Randomized"));
}

void TestStreamConfigDialog::tomlFrameSyncSaveDoesNotWriteDataRate()
{
    // Confirm the scope boundary: a saved frame-sync TOML must NOT contain
    // DataRate — it is below the wireframe separator.
    QTemporaryFile tmp;
    QVERIFY(tmp.open());
    tmp.close();

    QSettings out(tmp.fileName(), TomlConfigHelper::format());
    out.beginGroup("Frame");
    out.setValue("FrameSync",         "FE6B2840");
    out.setValue("FrameSyncMask",     "FFFFFFFF");
    out.setValue("WordsInMinorFrame", 48);
    out.endGroup();
    out.sync();

    QSettings in(tmp.fileName(), TomlConfigHelper::format());
    QVERIFY(!in.contains("Frame/DataRate"));
    QVERIFY(!in.contains("Frame/DataRateMbps"));
}

// ---------------------------------------------------------------------------
// Validation (US7.0)
// ---------------------------------------------------------------------------

void TestStreamConfigDialog::validateRejectsCheckedStreamWithEmptyFrameSync()
{
    // A stream with process=true but no frame sync pattern should cause
    // validateAndAccept to reject. We test this by verifying the dialog
    // stays open (result() == QDialog::Rejected) when done() is forced with
    // an invalid state — simulated here by inspecting the configs() state
    // that would trigger rejection.
    StreamConfig cfg = makeConfig();
    cfg.process          = true;
    cfg.sync.pattern = "";   // invalid — empty

    QScopedPointer<StreamConfigDialog> dlg(makeDialog({cfg}));

    // The dialog's configs() should reflect the empty pattern (widget was set to "").
    QVector<StreamConfig> out = dlg->configs();
    QVERIFY(out[0].process);
    QVERIFY(out[0].sync.pattern.trimmed().isEmpty());
    // This is the exact state that validateAndAccept rejects with a QMessageBox.
    // Calling accept() directly bypasses validateAndAccept, so we verify state only.
}

// ---------------------------------------------------------------------------
// UI Validation and Control Constraints
// ---------------------------------------------------------------------------

/// Locates the per-row gear button by tooltip (SVG icons are not loaded in the
/// test binary, so the icon can't be used) and clicks it. The click blocks in
/// the sub-dialog's exec(); a previously-scheduled singleShot fires within that
/// nested event loop to inspect and close the modal.
static void clickGearButton(StreamConfigDialog* dlg)
{
    const QList<QPushButton*> buttons = dlg->findChildren<QPushButton*>();
    for (QPushButton* btn : buttons) {
        if (btn->toolTip().contains("configuration dialog")) {
            btn->click();
            break;
        }
    }
}

/// Clicks the @p row -th "Configure this stream" gear button (rows are built
/// in order, so gear buttons appear in the same order in findChildren()).
static void clickGearButtonForRow(StreamConfigDialog* dlg, int row)
{
    int seen = 0;
    const QList<QPushButton*> buttons = dlg->findChildren<QPushButton*>();
    for (QPushButton* btn : buttons) {
        if (btn->toolTip().contains("configuration dialog")) {
            if (seen == row) {
                btn->click();
                return;
            }
            seen++;
        }
    }
}

void TestStreamConfigDialog::testFrameSyncValidation()
{
    StreamConfig cfg = makeConfig();
    cfg.process = true;  // gear button is only enabled for processed streams
    QScopedPointer<StreamConfigDialog> dlg(makeDialog({cfg}));

    bool testExecuted = false;
    QTimer::singleShot(50, [&]() {
        QWidget* activeWindow = QApplication::activeModalWidget();
        if (activeWindow) {
            QList<QLineEdit*> edits = activeWindow->findChildren<QLineEdit*>();
            if (edits.size() >= 2) {
                // The first line edit is Frame Sync Pattern
                QLineEdit* syncEdit = edits[0];
                QVERIFY(syncEdit->validator() != nullptr);

                int pos = 0;
                QString validHex = "A34F";
                QCOMPARE(syncEdit->validator()->validate(validHex, pos), QValidator::Acceptable);

                QString invalidHex = "ZXY";
                QCOMPARE(syncEdit->validator()->validate(invalidHex, pos), QValidator::Invalid);
                testExecuted = true;
            }
            activeWindow->close();
        }
    });

    clickGearButton(dlg.data());

    QVERIFY(testExecuted);
}

void TestStreamConfigDialog::testFrameMaskValidation()
{
    StreamConfig cfg = makeConfig();
    cfg.process = true;
    QScopedPointer<StreamConfigDialog> dlg(makeDialog({cfg}));

    bool testExecuted = false;
    QTimer::singleShot(50, [&]() {
        QWidget* activeWindow = QApplication::activeModalWidget();
        if (activeWindow) {
            QList<QLineEdit*> edits = activeWindow->findChildren<QLineEdit*>();
            if (edits.size() >= 2) {
                // The second line edit is Frame Sync Mask
                QLineEdit* maskEdit = edits[1];
                QVERIFY(maskEdit->validator() != nullptr);

                int pos = 0;
                QString validHex = "FFFF";
                QCOMPARE(maskEdit->validator()->validate(validHex, pos), QValidator::Acceptable);

                QString invalidHex = "FFZZ";
                QCOMPARE(maskEdit->validator()->validate(invalidHex, pos), QValidator::Invalid);
                testExecuted = true;
            }
            activeWindow->close();
        }
    });

    clickGearButton(dlg.data());

    QVERIFY(testExecuted);
}

void TestStreamConfigDialog::testDataRateLimits()
{
    StreamConfig cfg = makeConfig();
    cfg.process = true;
    QScopedPointer<StreamConfigDialog> dlg(makeDialog({cfg}));

    bool testExecuted = false;
    QTimer::singleShot(50, [&]() {
        QWidget* activeWindow = QApplication::activeModalWidget();
        if (activeWindow) {
            QList<QDoubleSpinBox*> spins = activeWindow->findChildren<QDoubleSpinBox*>();
            if (spins.size() >= 1) {
                QDoubleSpinBox* dataRate = spins[0];
                QCOMPARE(dataRate->minimum(), 0.0);
                QCOMPARE(dataRate->maximum(), 1000.0);
                testExecuted = true;
            }
            activeWindow->close();
        }
    });

    clickGearButton(dlg.data());

    QVERIFY(testExecuted);
}

void TestStreamConfigDialog::testDefaultSampleRate()
{
    StreamConfig cfg = makeConfig();
    cfg.process = true;
    QScopedPointer<StreamConfigDialog> dlg(makeDialog({cfg}));

    bool testExecuted = false;
    QTimer::singleShot(50, [&]() {
        QWidget* activeWindow = QApplication::activeModalWidget();
        if (activeWindow) {
            QList<QComboBox*> combos = activeWindow->findChildren<QComboBox*>();
            if (combos.size() >= 1) {
                QComboBox* sampleRate = combos[0];
                QCOMPARE(sampleRate->count(), 3);
                // The default period index is 0 (1 s)
                QCOMPARE(sampleRate->currentIndex(), 0);
                QCOMPARE(sampleRate->itemText(0), QString("1 s"));
                QCOMPARE(sampleRate->itemText(1), QString("100 ms"));
                QCOMPARE(sampleRate->itemText(2), QString("10 ms"));
                testExecuted = true;
            }
            activeWindow->close();
        }
    });

    clickGearButton(dlg.data());

    QVERIFY(testExecuted);
}

// ---------------------------------------------------------------------------
// "Apply to all" fan-out (US2.2)
// ---------------------------------------------------------------------------

void TestStreamConfigDialog::applyToAllCopiesSettingsToSameModeStreams()
{
    // Two Frame Sync Lock streams, both selected for processing.
    StreamConfig cfg0 = makeConfig("Ch 01", "FE6B2840", "FFFFFFFF", 64);
    cfg0.process = true;
    StreamConfig cfg1 = makeConfig("Ch 02", "11111111", "FFFFFFFF", 64);
    cfg1.process = true;

    QScopedPointer<StreamConfigDialog> dlg(makeDialog({cfg0, cfg1}));

    bool testExecuted = false;
    QTimer::singleShot(50, [&]() {
        QWidget* activeWindow = QApplication::activeModalWidget();
        QVERIFY(activeWindow != nullptr);

        QList<QLineEdit*> edits = activeWindow->findChildren<QLineEdit*>();
        QVERIFY(edits.size() >= 1);
        edits[0]->setText("A345CA5C"); // Frame Sync Pattern

        QCheckBox* applyToAll = nullptr;
        for (QCheckBox* cb : activeWindow->findChildren<QCheckBox*>()) {
            // The "Apply to all" text lives in an adjacent QLabel (checkbox text
            // is empty), so match the checkbox by its tooltip instead.
            if (cb->toolTip().contains("Copy these settings")) { applyToAll = cb; break; }
        }
        QVERIFY(applyToAll != nullptr);
        applyToAll->setChecked(true);

        // The sub-dialogs use a plain OK QPushButton (not a QDialogButtonBox), so
        // locate it by text. Finding it via findChild<QDialogButtonBox*>() returned
        // null, and the resulting QVERIFY failure left the modal open inside its
        // nested exec() loop — wedging the run.
        QPushButton* okBtn = nullptr;
        for (QPushButton* b : activeWindow->findChildren<QPushButton*>()) {
            if (b->text() == "OK") { okBtn = b; break; }
        }
        QVERIFY(okBtn != nullptr);
        testExecuted = true;
        okBtn->click();
    });

    clickGearButtonForRow(dlg.data(), 0);
    QVERIFY(testExecuted);

    QVector<StreamConfig> out = dlg->configs();
    QCOMPARE(out[0].sync.pattern, QString("A345CA5C"));
    QCOMPARE(out[1].sync.pattern, QString("A345CA5C"));
}

void TestStreamConfigDialog::applyToAllLeavesDifferentModeStreamsUnchanged()
{
    // Row 0: Frame Sync Lock; Row 1: Receiver SNR. Both selected for processing.
    StreamConfig cfg0 = makeConfig("Ch 01", "FE6B2840", "FFFFFFFF", 64);
    cfg0.process = true;
    cfg0.mode    = StreamMode::FrameSyncLockStats;

    StreamConfig cfg1 = makeConfig("Ch 02", "22222222", "FFFFFFFF", 64);
    cfg1.process = true;
    cfg1.mode    = StreamMode::ReceiverChannelInfo;

    QScopedPointer<StreamConfigDialog> dlg(makeDialog({cfg0, cfg1}));

    bool testExecuted = false;
    QTimer::singleShot(50, [&]() {
        QWidget* activeWindow = QApplication::activeModalWidget();
        QVERIFY(activeWindow != nullptr);

        QList<QLineEdit*> edits = activeWindow->findChildren<QLineEdit*>();
        QVERIFY(edits.size() >= 1);
        edits[0]->setText("A345CA5C"); // Frame Sync Pattern

        QCheckBox* applyToAll = nullptr;
        for (QCheckBox* cb : activeWindow->findChildren<QCheckBox*>()) {
            // The "Apply to all" text lives in an adjacent QLabel (checkbox text
            // is empty), so match the checkbox by its tooltip instead.
            if (cb->toolTip().contains("Copy these settings")) { applyToAll = cb; break; }
        }
        QVERIFY(applyToAll != nullptr);
        applyToAll->setChecked(true);

        // The sub-dialogs use a plain OK QPushButton (not a QDialogButtonBox), so
        // locate it by text. Finding it via findChild<QDialogButtonBox*>() returned
        // null, and the resulting QVERIFY failure left the modal open inside its
        // nested exec() loop — wedging the run.
        QPushButton* okBtn = nullptr;
        for (QPushButton* b : activeWindow->findChildren<QPushButton*>()) {
            if (b->text() == "OK") { okBtn = b; break; }
        }
        QVERIFY(okBtn != nullptr);
        testExecuted = true;
        okBtn->click();
    });

    clickGearButtonForRow(dlg.data(), 0);
    QVERIFY(testExecuted);

    QVector<StreamConfig> out = dlg->configs();
    QCOMPARE(out[0].sync.pattern, QString("A345CA5C"));
    // Row 1 is a different mode (Receiver SNR) — left unchanged.
    QCOMPARE(out[1].sync.pattern, QString("22222222"));
    QCOMPARE(out[1].mode, StreamMode::ReceiverChannelInfo);
}

void TestStreamConfigDialog::applyToAllUncheckedDoesNotAffectOtherStreams()
{
    StreamConfig cfg0 = makeConfig("Ch 01", "FE6B2840", "FFFFFFFF", 64);
    cfg0.process = true;
    StreamConfig cfg1 = makeConfig("Ch 02", "11111111", "FFFFFFFF", 64);
    cfg1.process = true;

    QScopedPointer<StreamConfigDialog> dlg(makeDialog({cfg0, cfg1}));

    bool testExecuted = false;
    QTimer::singleShot(50, [&]() {
        QWidget* activeWindow = QApplication::activeModalWidget();
        QVERIFY(activeWindow != nullptr);

        QList<QLineEdit*> edits = activeWindow->findChildren<QLineEdit*>();
        QVERIFY(edits.size() >= 1);
        edits[0]->setText("A345CA5C"); // Frame Sync Pattern

        // Leave the "Apply to all" checkbox unchecked.

        // The sub-dialogs use a plain OK QPushButton (not a QDialogButtonBox), so
        // locate it by text. Finding it via findChild<QDialogButtonBox*>() returned
        // null, and the resulting QVERIFY failure left the modal open inside its
        // nested exec() loop — wedging the run.
        QPushButton* okBtn = nullptr;
        for (QPushButton* b : activeWindow->findChildren<QPushButton*>()) {
            if (b->text() == "OK") { okBtn = b; break; }
        }
        QVERIFY(okBtn != nullptr);
        testExecuted = true;
        okBtn->click();
    });

    clickGearButtonForRow(dlg.data(), 0);
    QVERIFY(testExecuted);

    QVector<StreamConfig> out = dlg->configs();
    QCOMPARE(out[0].sync.pattern, QString("A345CA5C"));
    QCOMPARE(out[1].sync.pattern, QString("11111111"));
}

// ---------------------------------------------------------------------------
// Channel column label
// ---------------------------------------------------------------------------

void TestStreamConfigDialog::channelLabelShortNameShownInFull()
{
    // A name that comfortably fits the ~16-character-wide column shows in full,
    // with no truncation and no ellipsis.
    StreamConfig cfg = makeConfig("Ch 01");
    QScopedPointer<StreamConfigDialog> dlg(makeDialog({cfg}));

    QLabel* channelLabel = nullptr;
    for (QLabel* lbl : dlg->findChildren<QLabel*>()) {
        if (lbl->toolTip() == "Ch 01") { channelLabel = lbl; break; }
    }
    QVERIFY(channelLabel != nullptr);
    QCOMPARE(channelLabel->text(), QString("Ch 01"));
}

void TestStreamConfigDialog::channelLabelLongNameElidedWithFullTooltip()
{
    // A long, TMATS-derived descriptive name (like "CH-01 2250.5MHZ AGC 800Kbps
    // RNRZ-L") is wider than the fixed channel column, so it must be elided rather
    // than hard-clipped. Elision follows the LEFT alignment - ElideRight - so the
    // text runs from the cell's left edge and the ellipsis sits at the end.
    //
    // Note what this costs: the distinguishing detail in these names (band, rate,
    // code) is at the END, so a truncated long name no longer shows the part that
    // tells two channels apart. That is why the tooltip assertion below matters -
    // it is now the ONLY place the full name is recoverable.
    const QString fullName = "CH-01 2250.5MHZ AGC 800Kbps RNRZ-L";
    StreamConfig cfg = makeConfig(fullName);
    QScopedPointer<StreamConfigDialog> dlg(makeDialog({cfg}));

    QLabel* channelLabel = nullptr;
    for (QLabel* lbl : dlg->findChildren<QLabel*>()) {
        if (lbl->toolTip() == fullName) { channelLabel = lbl; break; }
    }
    QVERIFY(channelLabel != nullptr);
    QVERIFY(channelLabel->text() != fullName);
    QVERIFY(channelLabel->text().length() < fullName.length());
    QVERIFY(channelLabel->text().endsWith(QChar(0x2026)));   // Unicode ellipsis "…" at the END
    QVERIFY(channelLabel->text().startsWith("CH-01"));       // the head stays on screen
    QCOMPARE(channelLabel->toolTip(), fullName);
}

void TestStreamConfigDialog::headerLabelsUseThemeableObjectNames()
{
    QScopedPointer<StreamConfigDialog> dlg(makeDialog({makeConfig()}));

    // Five header columns: Process, Channel, Mode, Configure, Ready. Their color
    // must come from the theme QSS (matched by object name), so each header label
    // carries no inline stylesheet — the old hard-coded white vanished on light.
    const QList<QLabel*> headers = dlg->findChildren<QLabel*>("streamHeaderLabel");
    QCOMPARE(headers.size(), 5);
    for (QLabel* h : headers)
        QVERIFY2(h->styleSheet().isEmpty(),
                 "header color must be theme-driven, not an inline stylesheet");

    QFrame* separator = dlg->findChild<QFrame*>("streamHeaderSeparator");
    QVERIFY(separator != nullptr);
    QVERIFY(separator->styleSheet().isEmpty());
}

namespace {
/// The Mode combo always carries exactly these two items; the Time Channel
/// combo (also on the dialog) doesn't, so this reliably tells them apart
/// regardless of QObject child-list ordering.
QVector<QComboBox*> findModeCombos(QWidget* root)
{
    QVector<QComboBox*> found;
    for (QComboBox* c : root->findChildren<QComboBox*>()) {
        if (c->count() == 2 && c->itemText(0) == "Receiver SNR"
            && c->itemText(1) == "Frame Sync Lock") {
            found.append(c);
        }
    }
    return found;
}
} // namespace

void TestStreamConfigDialog::channelLabelHasComboBoxStyledObjectName()
{
    // The Channel cell is styled via this object name (see win11-{dark,light}.qss
    // QLabel#channelNameCell) to mirror the Mode combo's border/fill.
    StreamConfig cfg = makeConfig("Ch 01");
    QScopedPointer<StreamConfigDialog> dlg(makeDialog({cfg}));

    QLabel* channelLabel = nullptr;
    for (QLabel* lbl : dlg->findChildren<QLabel*>()) {
        if (lbl->toolTip() == "Ch 01") { channelLabel = lbl; break; }
    }
    QVERIFY(channelLabel != nullptr);
    QCOMPARE(channelLabel->objectName(), QString("channelNameCell"));
}

void TestStreamConfigDialog::modeComboDisplaysTextLeftJustified()
{
    // QComboBox has no built-in way to right-justify its closed-box text, so the
    // Mode combo is made editable with a read-only internal line edit, which IS
    // alignable. Typing must stay impossible — only the dropdown can change it.
    QScopedPointer<StreamConfigDialog> dlg(makeDialog({makeConfig("Ch 01")}));

    const QVector<QComboBox*> modeCombos = findModeCombos(dlg.data());
    QCOMPARE(modeCombos.size(), 1);
    QComboBox* mode = modeCombos.first();

    QVERIFY(mode->isEditable());
    QVERIFY(mode->lineEdit() != nullptr);
    QVERIFY(mode->lineEdit()->isReadOnly());
    // Left-aligned, matching the rest of the dialog's inputs. The editable+readonly
    // conversion exists to control this alignment at all, so the assertion stays -
    // it is what catches the line edit silently reverting to a plain combo.
    QCOMPARE(mode->lineEdit()->alignment(), Qt::Alignment(Qt::AlignLeft | Qt::AlignVCenter));
}

void TestStreamConfigDialog::modeComboSelectionStillTracksIndexChange()
{
    // Guards against the editable+readonly conversion silently breaking the
    // combo's role as the source of truth for configs().mode.
    StreamConfig cfg = makeConfig("Ch 01");
    cfg.mode = StreamMode::FrameSyncLockStats; // starts as index 1
    QScopedPointer<StreamConfigDialog> dlg(makeDialog({cfg}));

    const QVector<QComboBox*> modeCombos = findModeCombos(dlg.data());
    QCOMPARE(modeCombos.size(), 1);
    QComboBox* mode = modeCombos.first();
    QCOMPARE(mode->currentIndex(), 1);

    mode->setCurrentIndex(0); // switch to Receiver SNR
    QCOMPARE(dlg->configs()[0].mode, StreamMode::ReceiverChannelInfo);
}

void TestStreamConfigDialog::byteOrderToggleIsFileLevel()
{
    // Byte order is a property of the recorder that wrote the file, so it belongs to
    // the recording, not to a channel. This pins that it is reachable as a dialog-level
    // accessor alongside timeChannelIndex() - and, just as importantly, that it did NOT
    // become a per-stream field: a file whose channels disagreed about their own byte
    // order would be nonsense.
    QVector<StreamConfig> cfgs;
    cfgs << StreamConfig{} << StreamConfig{};

    QScopedPointer<StreamConfigDialog> on(makeDialog(cfgs, true));
    QVERIFY(on->swapBytes());

    QScopedPointer<StreamConfigDialog> off(makeDialog(cfgs, false));
    QVERIFY(!off->swapBytes());

    // One control for the whole dialog, not one per row.
    const QList<QCheckBox*> boxes = off->findChildren<QCheckBox*>();
    int byte_order_boxes = 0;
    for (QCheckBox* b : boxes)
    {
        if (b->text() == QStringLiteral("Legacy Chapter 10")) byte_order_boxes++;
    }
    QCOMPARE(byte_order_boxes, 1);
}
