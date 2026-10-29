/**
 * @file tst_framesyncfile.cpp
 * @brief Implementation of the frame-sync pattern file tests.
 */

#include "tst_framesyncfile.h"

#include <limits>

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QSpinBox>
#include <QtTest>

#include "constants.h"
#include "framesyncfile.h"
#include "qtbridge.h"
#include "streamsubdialogs.h"
#include "tomldocument.h"

namespace {

// ---------------------------------------------------------------------------
// Helpers. Everything the tests do with the file format goes through these, so a move of the code
// behind them changes these bodies and no assertion.
// ---------------------------------------------------------------------------

QString writeBytes(const QString& name, const QByteArray& bytes)
{
    const QString path = QDir::tempPath() + "/tst_framesyncfile_" + name + ".toml";
    QFile::remove(path);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(bytes);
    f.close();
    return path;
}

QByteArray readBytes(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QString escaped(const QByteArray& bytes)
{
    QString out;
    for (const char c : bytes)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        if (c == '\r')
            out += "\\r";
        else if (c == '\n')
            out += "\\n";
        else if (c == '\\')
            out += "\\\\";
        else if (u < 0x20 || u >= 0x7F)
            out += QString("\\x%1").arg(u, 2, 16, QChar('0'));
        else
            out += QChar(u);
    }
    return out;
}

QString asciiOnly(const QString& s)
{
    QString out;
    for (const QChar c : s)
        out += (c.unicode() < 0x80) ? QString(c) : QString("<U+%1>").arg(uint(c.unicode()), 4, 16, QChar('0')).toUpper();
    return out;
}

/// The widgets a pattern file is loaded into, set to values no file will produce so that "left alone"
/// is visible.
struct Fields
{
    QLineEdit sync;
    QLineEdit mask;
    QSpinBox bits;
    QCheckBox inverted;
    QCheckBox randomized;

    explicit Fields(bool randomized_before)
    {
        sync.setText("INIT-S");
        mask.setText("INIT-M");
        bits.setRange(PCMConstants::kMinFrameLengthBits, 65536);
        bits.setValue(1234);
        inverted.setChecked(true);
        randomized.setChecked(randomized_before);
    }

    QString digest() const
    {
        return QString("sync=%1 mask=%2 bits=%3 inv=%4 rand=%5")
            .arg(asciiOnly(sync.text()), asciiOnly(mask.text()))
            .arg(bits.value()).arg(inverted.isChecked()).arg(randomized.isChecked());
    }
};

/// Loads @p path as the dialog does, twice - with Derandomize ticked and unticked beforehand, because
/// "leave the operator's choice alone" only shows against both.
QString loadDigest(const QString& path)
{
    QString out;
    for (const bool before : { true, false })
    {
        Fields f(before);
        QString dir;
        loadFrameSyncFromToml(path, &f.sync, &f.mask, &f.bits, dir, &f.inverted, &f.randomized);
        out += (out.isEmpty() ? "" : " || ") + f.digest();
    }
    return out;
}

/// The same load with no Invert/Derandomize boxes to fill (the parameters are optional).
QString loadWithoutFlagBoxesDigest(const QString& path)
{
    Fields f(true);
    QString dir;
    loadFrameSyncFromToml(path, &f.sync, &f.mask, &f.bits, dir);
    return f.digest();
}

/// What saving writes, as bytes.
QString saveDigest(const QString& pattern, const QString& mask, int bits, bool inverted, bool randomized)
{
    const QString path = QDir::tempPath() + "/tst_framesyncfile_saved.toml";
    QFile::remove(path);
    QString dir;
    saveFrameSyncToToml(path, pattern, mask, bits, inverted, randomized, dir);
    const QString out = escaped(readBytes(path)) + (QFile::exists(path) ? "" : "(no file)");
    QFile::remove(path);
    return out;
}

/// The Receiver SNR defaults read from an app root holding @p bytes as
/// settings/framesync_patterns/framesync_rcvr_default.toml (or nothing, if @p present is false).
QString receiverDefaultsDigest(const QByteArray& bytes, bool present)
{
    const QString root = QDir::tempPath() + "/tst_framesyncfile_root";
    QDir(root).removeRecursively();
    const QString dir = root + "/" + UIConstants::kSettingsDirName + "/" + UIConstants::kFramesyncPatternsDirName;
    QDir().mkpath(dir);
    if (present)
    {
        QFile f(dir + "/" + UIConstants::kDefaultReceiverFrameSyncFilename);
        if (!f.open(QIODevice::WriteOnly))
            return "cannot write the fixture";
        f.write(bytes);
        f.close();
    }
    const ReceiverFrameDefaults d = loadReceiverFrameDefaults(root);
    QDir(root).removeRecursively();
    return QString("pattern=%1 mask=%2 bits=%3").arg(asciiOnly(d.pattern), asciiOnly(d.mask)).arg(d.bits);
}

QByteArray frame(const QByteArray& body)
{
    return "[Frame]\n" + body;
}

QByteArray utf16le(const QByteArray& ascii)
{
    QByteArray out("\xFF\xFE", 2);
    for (const char c : ascii)
    {
        out += c;
        out += '\0';
    }
    return out;
}

// ---------------------------------------------------------------------------
// Load cases
// ---------------------------------------------------------------------------

struct Case
{
    const char* name;
    QByteArray bytes;
    bool present;
    const char* expect;
};

const QList<Case>& loadCases()
{
    static const QList<Case> cases = {
        { "file-missing", {}, false, "sync=INIT-S mask=INIT-M bits=1234 inv=0 rand=1 ||"
            " sync=INIT-S mask=INIT-M bits=1234 inv=0 rand=0" },
        { "file-empty", {}, true, "sync=INIT-S mask=INIT-M bits=1234 inv=0 rand=1 ||"
            " sync=INIT-S mask=INIT-M bits=1234 inv=0 rand=0" },
        { "a-complete-file", frame("FrameSync = \"FE6B2840\"\nFrameSyncMask = \"FFFF0000\"\nBitsPerFrame = 800\nInverted = true\nRandomized = true\n"), true, "sync=FE6B2840 mask=FFFF0000 bits=800 inv=1 rand=1 ||"
            " sync=FE6B2840 mask=FFFF0000 bits=800 inv=1 rand=1" },
        { "a-complete-file-with-both-flags-off", frame("FrameSync = \"FE6B2840\"\nFrameSyncMask = \"FFFF0000\"\nBitsPerFrame = 800\nInverted = false\nRandomized = false\n"), true, "sync=FE6B2840 mask=FFFF0000 bits=800 inv=0 rand=0 ||"
            " sync=FE6B2840 mask=FFFF0000 bits=800 inv=0 rand=0" },
        { "utf8-bom", QByteArray("\xEF\xBB\xBF") + frame("FrameSync = \"FE6B2840\"\nBitsPerFrame = 800\n"), true, "sync=FE6B2840 mask=INIT-M bits=800 inv=0 rand=1 ||"
            " sync=FE6B2840 mask=INIT-M bits=800 inv=0 rand=0" },

        // --- pattern and mask
        { "pattern-only", frame("FrameSync = \"334AABBF\"\n"), true, "sync=334AABBF mask=INIT-M bits=1234 inv=0 rand=1 ||"
            " sync=334AABBF mask=INIT-M bits=1234 inv=0 rand=0" },
        { "mask-only", frame("FrameSyncMask = \"FF00FF00\"\n"), true, "sync=INIT-S mask=FF00FF00 bits=1234 inv=0 rand=1 ||"
            " sync=INIT-S mask=FF00FF00 bits=1234 inv=0 rand=0" },
        { "lowercase-is-uppercased", frame("FrameSync = \"fe6b2840\"\nFrameSyncMask = \"ffff0000\"\n"), true, "sync=FE6B2840 mask=FFFF0000 bits=1234 inv=0 rand=1 ||"
            " sync=FE6B2840 mask=FFFF0000 bits=1234 inv=0 rand=0" },
        { "not-hex-is-kept-as-it-is", frame("FrameSync = \"zz-1\"\nFrameSyncMask = \"x\"\n"), true, "sync=ZZ-1 mask=X bits=1234 inv=0 rand=1 ||"
            " sync=ZZ-1 mask=X bits=1234 inv=0 rand=0" },
        { "non-ascii-lowercase-is-uppercased-as-qt-does", frame("FrameSync = \"stra\xC3\x9F" "e \xCE\xBB\"\n"), true, "sync=STRASSE <U+039B> mask=INIT-M bits=1234 inv=0 rand=1 ||"
            " sync=STRASSE <U+039B> mask=INIT-M bits=1234 inv=0 rand=0" },
        { "empty-pattern-leaves-the-field-alone", frame("FrameSync = \"\"\nFrameSyncMask = \"\"\n"), true, "sync=INIT-S mask=INIT-M bits=1234 inv=0 rand=1 ||"
            " sync=INIT-S mask=INIT-M bits=1234 inv=0 rand=0" },

        // --- the frame length
        { "bits-at-the-minimum", frame("BitsPerFrame = 64\n"), true, "sync=INIT-S mask=INIT-M bits=64 inv=0 rand=1 ||"
            " sync=INIT-S mask=INIT-M bits=64 inv=0 rand=0" },
        { "bits-below-the-minimum-is-ignored", frame("BitsPerFrame = 63\n"), true, "sync=INIT-S mask=INIT-M bits=1234 inv=0 rand=1 ||"
            " sync=INIT-S mask=INIT-M bits=1234 inv=0 rand=0" },
        { "bits-above-the-spin-box-maximum-clamps", frame("BitsPerFrame = 70000\n"), true, "sync=INIT-S mask=INIT-M bits=65536 inv=0 rand=1 ||"
            " sync=INIT-S mask=INIT-M bits=65536 inv=0 rand=0" },
        { "words-in-minor-frame-is-sixteen-bits-a-word", frame("WordsInMinorFrame = 64\n"), true, "sync=INIT-S mask=INIT-M bits=1024 inv=0 rand=1 ||"
            " sync=INIT-S mask=INIT-M bits=1024 inv=0 rand=0" },
        { "words-large-clamps", frame("WordsInMinorFrame = 100000\n"), true, "sync=INIT-S mask=INIT-M bits=65536 inv=0 rand=1 ||"
            " sync=INIT-S mask=INIT-M bits=65536 inv=0 rand=0" },
        { "bits-too-small-falls-back-to-words", frame("BitsPerFrame = 32\nWordsInMinorFrame = 64\n"), true, "sync=INIT-S mask=INIT-M bits=1024 inv=0 rand=1 ||"
            " sync=INIT-S mask=INIT-M bits=1024 inv=0 rand=0" },

        // --- the two stream-format flags
        { "inverted-true", frame("Inverted = true\n"), true, "sync=INIT-S mask=INIT-M bits=1234 inv=1 rand=1 ||"
            " sync=INIT-S mask=INIT-M bits=1234 inv=1 rand=0" },
        { "inverted-false", frame("Inverted = false\n"), true, "sync=INIT-S mask=INIT-M bits=1234 inv=0 rand=1 ||"
            " sync=INIT-S mask=INIT-M bits=1234 inv=0 rand=0" },
        { "inverted-absent-clears-it", frame("FrameSync = \"AB\"\n"), true, "sync=AB mask=INIT-M bits=1234 inv=0 rand=1 ||"
            " sync=AB mask=INIT-M bits=1234 inv=0 rand=0" },
        { "randomized-true", frame("Randomized = true\n"), true, "sync=INIT-S mask=INIT-M bits=1234 inv=0 rand=1 ||"
            " sync=INIT-S mask=INIT-M bits=1234 inv=0 rand=1" },
        { "randomized-false", frame("Randomized = false\n"), true, "sync=INIT-S mask=INIT-M bits=1234 inv=0 rand=0 ||"
            " sync=INIT-S mask=INIT-M bits=1234 inv=0 rand=0" },
        { "randomized-absent-leaves-the-choice-alone", frame("FrameSync = \"AB\"\nInverted = true\n"), true, "sync=AB mask=INIT-M bits=1234 inv=1 rand=1 ||"
            " sync=AB mask=INIT-M bits=1234 inv=1 rand=0" },
    };
    return cases;
}

}  // namespace

void TestFrameSyncFile::loadEdgeCasesKeepTheirRecordedBehaviour_data()
{
    QTest::addColumn<int>("index");
    for (int i = 0; i < loadCases().size(); ++i)
        QTest::newRow(loadCases()[i].name) << i;
}

void TestFrameSyncFile::loadEdgeCasesKeepTheirRecordedBehaviour()
{
    QFETCH(int, index);
    const Case& c = loadCases()[index];
    const QString path = c.present ? writeBytes("load", c.bytes) : QDir::tempPath() + "/tst_framesyncfile_no_such_file.toml";
    if (!c.present)
        QFile::remove(path);
    const QString got = loadDigest(path);
    QFile::remove(path);
    QCOMPARE(got, QString::fromLatin1(c.expect));
}

// ---------------------------------------------------------------------------
// Save cases
// ---------------------------------------------------------------------------

namespace {

struct SaveCase
{
    const char* name;
    QString pattern;
    QString mask;
    int bits;
    bool inverted;
    bool randomized;
    const char* expect;
};

const QList<SaveCase>& saveCases()
{
    static const QList<SaveCase> cases = {
        { "everything-on", "FE6B2840", "FFFFFFFF", 800, true, true, "[Frame]\\r\\n"
            "FrameSync = \"FE6B2840\"\\r\\n"
            "FrameSyncMask = \"FFFFFFFF\"\\r\\n"
            "BitsPerFrame = 800\\r\\n"
            "Inverted = true\\r\\n"
            "Randomized = true\\r\\n" },
        { "inverted-only", "334AABBF", "FFFFFFFF", 32767, true, false, "[Frame]\\r\\n"
            "FrameSync = \"334AABBF\"\\r\\n"
            "FrameSyncMask = \"FFFFFFFF\"\\r\\n"
            "BitsPerFrame = 32767\\r\\n"
            "Inverted = true\\r\\n"
            "Randomized = false\\r\\n" },
        { "randomized-only", "334AABBF", "FFFFFFFF", 32767, false, true, "[Frame]\\r\\n"
            "FrameSync = \"334AABBF\"\\r\\n"
            "FrameSyncMask = \"FFFFFFFF\"\\r\\n"
            "BitsPerFrame = 32767\\r\\n"
            "Inverted = false\\r\\n"
            "Randomized = true\\r\\n" },
        { "lowercase-is-written-as-typed", "fe6b2840", "ffff0000", 800, false, false, "[Frame]\\r\\n"
            "FrameSync = \"fe6b2840\"\\r\\n"
            "FrameSyncMask = \"ffff0000\"\\r\\n"
            "BitsPerFrame = 800\\r\\n"
            "Inverted = false\\r\\n"
            "Randomized = false\\r\\n" },
        { "non-ascii", QString::fromUtf8("\xCE\xA9\xC3\xA9"), "FF", 800, false, false, "[Frame]\\r\\n"
            "FrameSync = \"\\xce\\xa9\\xc3\\xa9\"\\r\\n"
            "FrameSyncMask = \"FF\"\\r\\n"
            "BitsPerFrame = 800\\r\\n"
            "Inverted = false\\r\\n"
            "Randomized = false\\r\\n" },
        { "padded", "  AB  ", " CD ", 800, false, false, "[Frame]\\r\\n"
            "FrameSync = \"  AB  \"\\r\\n"
            "FrameSyncMask = \" CD \"\\r\\n"
            "BitsPerFrame = 800\\r\\n"
            "Inverted = false\\r\\n"
            "Randomized = false\\r\\n" },
    };
    return cases;
}

}  // namespace

void TestFrameSyncFile::saveEdgeCasesKeepTheirRecordedBehaviour_data()
{
    QTest::addColumn<int>("index");
    for (int i = 0; i < saveCases().size(); ++i)
        QTest::newRow(saveCases()[i].name) << i;
}

void TestFrameSyncFile::saveEdgeCasesKeepTheirRecordedBehaviour()
{
    QFETCH(int, index);
    const SaveCase& c = saveCases()[index];
    const QString got = saveDigest(c.pattern, c.mask, c.bits, c.inverted, c.randomized);
    QCOMPARE(got, QString::fromLatin1(c.expect));
}

// ---------------------------------------------------------------------------
// Receiver defaults
// ---------------------------------------------------------------------------

namespace {

const QList<Case>& defaultsCases()
{
    static const QList<Case> cases = {
        { "no-file-gives-the-compiled-constants", {}, false, "pattern=FE6B2840 mask=FFFFFFFF bits=800" },
        { "empty-file", {}, true, "pattern=FE6B2840 mask=FFFFFFFF bits=800" },
        { "a-complete-file", frame("FrameSync = \"FE6B2840\"\nFrameSyncMask = \"FFFF0000\"\nBitsPerFrame = 800\n"), true, "pattern=FE6B2840 mask=FFFF0000 bits=800" },
        { "lowercase-is-uppercased", frame("FrameSync = \"fe6b2840\"\nFrameSyncMask = \"ffff0000\"\n"), true, "pattern=FE6B2840 mask=FFFF0000 bits=800" },
        { "pattern-only", frame("FrameSync = \"AB\"\n"), true, "pattern=AB mask=FFFFFFFF bits=800" },
        { "mask-only", frame("FrameSyncMask = \"CD\"\n"), true, "pattern=FE6B2840 mask=CD bits=800" },
        { "empty-values-keep-the-constants", frame("FrameSync = \"\"\nFrameSyncMask = \"\"\nBitsPerFrame = 800\n"), true, "pattern=FE6B2840 mask=FFFFFFFF bits=800" },
        { "bits-at-the-minimum", frame("BitsPerFrame = 64\n"), true, "pattern=FE6B2840 mask=FFFFFFFF bits=64" },
        { "bits-below-the-minimum-keeps-the-constant", frame("BitsPerFrame = 63\n"), true, "pattern=FE6B2840 mask=FFFFFFFF bits=800" },
        { "bits-above-the-spin-box-maximum-is-kept-as-it-is", frame("BitsPerFrame = 70000\n"), true, "pattern=FE6B2840 mask=FFFFFFFF bits=70000" },
        { "words-in-minor-frame-is-not-read-here", frame("WordsInMinorFrame = 64\n"), true, "pattern=FE6B2840 mask=FFFFFFFF bits=800" },
        { "bits-too-small-and-words-present", frame("BitsPerFrame = 32\nWordsInMinorFrame = 64\n"), true, "pattern=FE6B2840 mask=FFFFFFFF bits=800" },
        { "bits-at-the-minimum-still-beat-words", frame("BitsPerFrame = 64\nWordsInMinorFrame = 100\n"), true,
            "pattern=FE6B2840 mask=FFFFFFFF bits=64" },
        { "inverted-and-randomized-are-not-read-here", frame("FrameSync = \"AB\"\nInverted = true\nRandomized = true\n"), true, "pattern=AB mask=FFFFFFFF bits=800" },
        { "utf16le-bom", utf16le(frame("FrameSync = \"AB\"\nBitsPerFrame = 800\n")), true, "pattern=AB mask=FFFFFFFF bits=800" },
        { "keys-in-another-section-are-ignored", "[Other]\nFrameSync = \"AB\"\nBitsPerFrame = 800\n", true, "pattern=FE6B2840 mask=FFFFFFFF bits=800" },
    };
    return cases;
}

}  // namespace

void TestFrameSyncFile::receiverDefaultsEdgeCasesKeepTheirRecordedBehaviour_data()
{
    QTest::addColumn<int>("index");
    for (int i = 0; i < defaultsCases().size(); ++i)
        QTest::newRow(defaultsCases()[i].name) << i;
}

void TestFrameSyncFile::receiverDefaultsEdgeCasesKeepTheirRecordedBehaviour()
{
    QFETCH(int, index);
    const Case& c = defaultsCases()[index];
    const QString got = receiverDefaultsDigest(c.bytes, c.present);
    QCOMPARE(got, QString::fromLatin1(c.expect));
}

// ---------------------------------------------------------------------------
// The implementations the core replaced, kept here as oracles: the dialog's load and save and the
// Receiver SNR defaults reader, verbatim apart from their names. A difference with the code that
// replaced them is a change of behaviour and not a difference in what was copied.
// ---------------------------------------------------------------------------

namespace oracle {

void loadFrameSyncFromToml(const QString& filename, QLineEdit* syncEdit, QLineEdit* maskEdit, QSpinBox* bitsSpinBox,
                           QString& toml_dir, QCheckBox* invertedBox, QCheckBox* randomizedBox)
{
    toml_dir = QFileInfo(filename).absolutePath();
    const TomlDocument cfg = TomlDocument::load(toFsPath(filename));

    QString sync = QString::fromStdString(cfg.string("Frame/FrameSync"));
    if (!sync.isEmpty())
        syncEdit->setText(sync.toUpper());

    QString mask = QString::fromStdString(cfg.string("Frame/FrameSyncMask"));
    if (!mask.isEmpty())
        maskEdit->setText(mask.toUpper());

    int bits = cfg.integer("Frame/BitsPerFrame", 0);
    if (bits < PCMConstants::kMinFrameLengthBits)
    {
        // Fall back to old WordsInMinorFrame key (words x 16 bits)
        int words = cfg.integer("Frame/WordsInMinorFrame", 0);
        if (words >= 2)
            bits = words * PCMConstants::kCommonWordLen;
    }
    if (bits >= PCMConstants::kMinFrameLengthBits)
        bitsSpinBox->setValue(bits);

    if (invertedBox)
    {
        bool inv = cfg.boolean("Frame/Inverted", false);
        invertedBox->setChecked(inv);
    }

    if (randomizedBox && cfg.contains("Frame/Randomized"))
    {
        randomizedBox->setChecked(cfg.boolean("Frame/Randomized"));
    }
}

void saveFrameSyncToToml(const QString& filename, const QString& syncPattern, const QString& syncMask,
                         int bitsPerFrame, bool inverted, bool randomized, QString& toml_dir)
{
    toml_dir = QFileInfo(filename).absolutePath();
    TomlDocument cfg;
    cfg.setValue("Frame/FrameSync",     syncPattern.toStdString());
    cfg.setValue("Frame/FrameSyncMask", syncMask.toStdString());
    cfg.setValue("Frame/BitsPerFrame",  bitsPerFrame);
    cfg.setValue("Frame/Inverted",      inverted);
    cfg.setValue("Frame/Randomized",    randomized);
    cfg.save(toFsPath(filename));
}

ReceiverFrameDefaults loadReceiverFrameDefaults(const QString& app_root)
{
    ReceiverFrameDefaults d;
    const QString path = app_root + "/" + UIConstants::kSettingsDirName + "/" +
        UIConstants::kFramesyncPatternsDirName + "/" +
        UIConstants::kDefaultReceiverFrameSyncFilename;
    if (!QFileInfo::exists(path))
        return d;

    const TomlDocument cfg = TomlDocument::load(toFsPath(path));
    const QString sync = QString::fromStdString(cfg.string("Frame/FrameSync"));
    if (!sync.isEmpty())
        d.pattern = sync.toUpper();
    const QString mask = QString::fromStdString(cfg.string("Frame/FrameSyncMask"));
    if (!mask.isEmpty())
        d.mask = mask.toUpper();
    const int bits = cfg.integer("Frame/BitsPerFrame", 0);
    if (bits >= PCMConstants::kMinFrameLengthBits)
        d.bits = bits;
    return d;
}

}  // namespace oracle

namespace {

/// A deterministic generator, so a failure names the same case every run.
struct Rng
{
    quint32 state;
    explicit Rng(quint32 seed) : state(seed) {}
    quint32 next() { state = state * 1664525u + 1013904223u; return state >> 8; }
    int below(int n) { return int(next() % quint32(n)); }
    bool chance(int percent) { return below(100) < percent; }
    template <typename List>
    auto pick(const List& list) { return list[below(int(list.size()))]; }
};

/// A pattern file as a hand-edited or older one might be: keys present or not, in the right section or
/// another, with values from pools of the ordinary and the awkward, repeated, commented, with either
/// line ending and sometimes a byte-order mark.
QByteArray generateFile(Rng& rng)
{
    static const QStringList strings = {
        "\"FE6B2840\"", "\"fe6b2840\"", "\"\"", "FE6B2840", "\"  AB  \"", "\"stra\xC3\x9F" "e\"", "\"zz\"", "\"A#B\"",
        "123", "\"334AABBF\"", "\"FFFFFFFF\"", "\"ffff0000\"", "'single'",
    };
    static const QStringList bits = { "800", "64", "63", "0", "-5", "65536", "70000", "\"800\"", "800.5", "+800", "abc",
                                      "", "32", "2147483647", "4294967296", "32767", "1024" };
    static const QStringList words = { "64", "1", "2", "3", "4", "-64", "100000", "abc", "0", "\"64\"", "32767", "8", "" };
    static const QStringList flags = { "true", "false", "1", "0", "yes", "no", "TRUE", "\"true\"", "\"false\"", "", "maybe", "2", "False" };

    QString text;
    const int section = rng.below(100);
    if (section < 88)      text += "[Frame]\n";
    else if (section < 94) text += "[Other]\n";
    // else: no section at all

    auto key = [&](const char* name) {
        QString k = name;
        if (rng.chance(4))
            k = k.toLower();   // key case matters
        return k;
    };
    auto line = [&](const char* name, const QString& value) {
        QString l = key(name) + " = " + value;
        if (rng.chance(8))
            l += " # a comment";
        text += l + "\n";
    };

    struct Field { const char* name; const QStringList* pool; int presence; };
    const Field fields[] = {
        { "FrameSync", &strings, 70 }, { "FrameSyncMask", &strings, 55 }, { "BitsPerFrame", &bits, 65 },
        { "WordsInMinorFrame", &words, 30 }, { "Inverted", &flags, 50 }, { "Randomized", &flags, 50 },
    };
    for (const Field& f : fields)
    {
        if (!rng.chance(f.presence))
            continue;
        line(f.name, rng.pick(*f.pool));
        if (rng.chance(6))
            line(f.name, rng.pick(*f.pool));   // a repeated key: the last one wins
    }

    if (rng.chance(30))
        text.replace("\n", "\r\n");
    QByteArray out = text.toUtf8();
    if (rng.chance(5))
        out = QByteArray("\xEF\xBB\xBF") + out;
    return out;
}

}  // namespace

void TestFrameSyncFile::theCoreReadsEveryGeneratedFileLikeTheOldDialogCode()
{
    constexpr int kFiles = 300;
    Rng rng(20261026u);
    const QString path = QDir::tempPath() + "/tst_framesyncfile_generated.toml";
    int with_bits = 0, with_legacy_bits = 0, with_randomized = 0, inverted = 0, with_pattern = 0, ignored_bits = 0;
    QStringList problems;
    for (int n = 0; n < kFiles; ++n)
    {
        const QByteArray bytes = generateFile(rng);
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(bytes);
        f.close();

        for (const bool before : { true, false })
        {
            Fields want(before), got(before);
            QString want_dir, got_dir;
            oracle::loadFrameSyncFromToml(path, &want.sync, &want.mask, &want.bits, want_dir, &want.inverted, &want.randomized);
            loadFrameSyncFromToml(path, &got.sync, &got.mask, &got.bits, got_dir, &got.inverted, &got.randomized);
            if ((want.digest() != got.digest() || want_dir != got_dir) && problems.size() < 4)
                problems << QString("case %1 rand-before=%2 (%3)\n  old %4\n  new %5")
                                .arg(n).arg(before).arg(escaped(bytes.left(260)), want.digest(), got.digest());
        }

        // and the same file as the Receiver SNR defaults
        const QString root = QDir::tempPath() + "/tst_framesyncfile_gen_root";
        QDir(root).removeRecursively();
        const QString dir = root + "/" + UIConstants::kSettingsDirName + "/" + UIConstants::kFramesyncPatternsDirName;
        QDir().mkpath(dir);
        QVERIFY(QFile::copy(path, dir + "/" + UIConstants::kDefaultReceiverFrameSyncFilename));
        const ReceiverFrameDefaults want_defaults = oracle::loadReceiverFrameDefaults(root);
        const ReceiverFrameDefaults got_defaults = loadReceiverFrameDefaults(root);
        if ((want_defaults.pattern != got_defaults.pattern || want_defaults.mask != got_defaults.mask ||
             want_defaults.bits != got_defaults.bits) && problems.size() < 4)
            problems << QString("defaults case %1 (%2): old %3/%4/%5, new %6/%7/%8")
                            .arg(n).arg(escaped(bytes.left(260)), want_defaults.pattern, want_defaults.mask)
                            .arg(want_defaults.bits).arg(got_defaults.pattern, got_defaults.mask).arg(got_defaults.bits);
        QDir(root).removeRecursively();

        Fields probe(false);
        QString d;
        oracle::loadFrameSyncFromToml(path, &probe.sync, &probe.mask, &probe.bits, d, &probe.inverted, &probe.randomized);
        with_bits += probe.bits.value() != 1234;
        with_pattern += probe.sync.text() != "INIT-S";
        inverted += probe.inverted.isChecked();
        with_randomized += probe.randomized.isChecked();
        const FrameSyncFile file = readFrameSyncFile(toFsPath(path));
        with_legacy_bits += file.bitsFromLegacyKey && file.bitsPerFrame.has_value();
        ignored_bits += !file.bitsPerFrame.has_value();
    }
    QFile::remove(path);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join("\n")));
    // The generator must reach every branch in volume, or "no difference" proves nothing.
    QVERIFY2(with_bits > 60, qPrintable(QString::number(with_bits)));
    QVERIFY2(with_legacy_bits > 6, qPrintable(QString::number(with_legacy_bits)));
    QVERIFY2(ignored_bits > 15, qPrintable(QString::number(ignored_bits)));
    QVERIFY2(with_randomized > 20, qPrintable(QString::number(with_randomized)));
    QVERIFY2(inverted > 20, qPrintable(QString::number(inverted)));
    QVERIFY2(with_pattern > 80, qPrintable(QString::number(with_pattern)));
}

void TestFrameSyncFile::theCoreWritesEveryGeneratedFileLikeTheOldDialogCode()
{
    Rng rng(20261027u);
    static const QStringList texts = { "FE6B2840", "fe6b2840", "", "  AB  ", "A\"B\\C", "A#B=C", "A\nB", "334AABBF", "FFFFFFFF",
                                       QString::fromUtf8("\xCE\xA9\xC3\xA9"), "x y", "'q'" };
    static const QList<int> bits = { 0, 1, 63, 64, 800, 4096, 32767, 65536, -1, -800, 2147483647, std::numeric_limits<int>::min() };
    const QString path_old = QDir::tempPath() + "/tst_framesyncfile_old_write.toml";
    const QString path_new = QDir::tempPath() + "/tst_framesyncfile_new_write.toml";
    int different_flags = 0, odd_text = 0;
    for (int n = 0; n < 300; ++n)
    {
        const QString pattern = rng.pick(texts), mask = rng.pick(texts);
        const int b = rng.pick(bits);
        const bool inv = rng.chance(50), rnd = rng.chance(50);
        QFile::remove(path_old);
        QFile::remove(path_new);
        QString d1, d2;
        oracle::saveFrameSyncToToml(path_old, pattern, mask, b, inv, rnd, d1);
        saveFrameSyncToToml(path_new, pattern, mask, b, inv, rnd, d2);
        const QByteArray old_bytes = readBytes(path_old);
        const QByteArray new_bytes = readBytes(path_new);
        QVERIFY2(old_bytes == new_bytes && d1 == d2,
                 qPrintable(QString("case %1: old %2 / new %3").arg(n).arg(escaped(old_bytes), escaped(new_bytes))));
        QVERIFY(!old_bytes.isEmpty());
        different_flags += inv != rnd;
        odd_text += pattern.contains('"') || pattern.contains('\n') || pattern.isEmpty();
    }
    QFile::remove(path_old);
    QFile::remove(path_new);
    QVERIFY2(different_flags > 60, qPrintable(QString::number(different_flags)));
    QVERIFY2(odd_text > 40, qPrintable(QString::number(odd_text)));
}

void TestFrameSyncFile::aFileWrittenByTheDialogLoadsBackIntoIt()
{
    // Save then load: what was typed comes back, the flags included, and nothing else changes.
    const QString path = QDir::tempPath() + "/tst_framesyncfile_roundtrip.toml";
    QFile::remove(path);
    QString dir;
    saveFrameSyncToToml(path, "FE6B2840", "FFFF0000", 4096, /*inverted=*/true, /*randomized=*/true, dir);
    QCOMPARE(dir, QFileInfo(path).absolutePath());

    Fields f(false);
    QString loaded_dir;
    loadFrameSyncFromToml(path, &f.sync, &f.mask, &f.bits, loaded_dir, &f.inverted, &f.randomized);
    QFile::remove(path);
    QCOMPARE(f.sync.text(), QString("FE6B2840"));
    QCOMPARE(f.mask.text(), QString("FFFF0000"));
    QCOMPARE(f.bits.value(), 4096);
    QVERIFY(f.inverted.isChecked());
    QVERIFY(f.randomized.isChecked());
    QCOMPARE(loaded_dir, QFileInfo(path).absolutePath());
}
