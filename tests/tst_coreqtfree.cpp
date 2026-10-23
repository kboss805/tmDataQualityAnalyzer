#include "tst_coreqtfree.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QQueue>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

namespace {

/// The model sources that make up the processing core: everything the decode pipeline runs, from
/// the file readers to the calibration extraction. They must compile with no Qt.
const QStringList kCoreSources = {
    "calibrationextractor", "ch10packetreader", "channeldata", "chapter10reader", "clocktime",
    "csvseriesparser", "csvserieswriter", "frameprocessor", "framesetup", "framesyncfile", "pipelinerunner",
    "seriesbuilder", "seriescolumnschema", "stepdetector", "streamjobbuilder", "templatematcher", "textfile", "tomldocument",
};

/// The model sources that are, on purpose, still Qt: they read and write UI-session state (processing
/// templates, stream-config JSON) and are not part of the decode pipeline. Listing them is what makes a
/// new model file have to be classified.
const QStringList kUiSessionSources = {
    "processingtemplateschema", "streamconfigschema",
};

QString projectRoot()
{
    QDir dir(QCoreApplication::applicationDirPath());
    dir.cdUp();   // tests/
    dir.cdUp();   // project root
    return dir.absolutePath();
}

/// The directories a quoted include is looked up in, as the build's include path has them. The
/// first four are the core's; the last two are the layers it must NOT reach into.
struct IncludeDirs
{
    QStringList core;
    QStringList forbidden;
};

IncludeDirs includeDirsOf(const QString& root)
{
    IncludeDirs dirs;
    dirs.core      = {root + "/include", root + "/include/dto", root + "/include/model",
                      root + "/lib/irig106/include"};
    dirs.forbidden = {root + "/include/viewmodel", root + "/include/view"};
    return dirs;
}

/// What the include closure of one source file turned up.
struct Closure
{
    QStringList qtIncludes;      ///< "file: #include <QX>" for every Qt header found.
    QStringList layerViolations; ///< "file: includes <header>" for each ViewModel / View header reached.
    int         filesVisited = 0;
    QStringList visited;         ///< Project-relative path of every file the walk reached.
};

/// <QString>, <QtGlobal>, <QObject> ... but not a lower-case second letter such as <Quux.h>: a Qt header
/// is Q followed by a capital letter (or Qt itself).
bool isQtHeader(const QString& name)
{
    return name.size() > 1 && name[0] == 'Q' && (name[1].isUpper() || name.startsWith("Qt"));
}

/// "QJsonObject" for every `class QJsonObject;` / `struct QX;` forward declaration in @p text: a header
/// that forward-declares a Qt class has Qt in its interface without including anything.
QStringList forwardDeclaredQtClasses(const QString& text)
{
    static const QRegularExpression forward(
        QStringLiteral("^[ \\t]*(?:class|struct)[ \\t]+(Q[A-Z][A-Za-z0-9_]*)[ \\t]*;"),
        QRegularExpression::MultilineOption);
    QStringList found;
    auto it = forward.globalMatch(text);
    while (it.hasNext())
        found << it.next().captured(1);
    return found;
}

QString readText(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(file.readAll());
}

/// Walks the project-local `#include "..."` closure of @p start, noting every Qt header
/// (`#include <Q...>`) on the way. A quoted include is found next to the including file, then in the
/// core's include directories, then in the forbidden ones (where it is recorded as a violation).
Closure walk(const QString& start, const QString& root)
{
    static const QRegularExpression include_line(
        QStringLiteral("^[ \\t]*#[ \\t]*include[ \\t]*([<\"])([^>\"]+)[>\"]"),
        QRegularExpression::MultilineOption);
    const IncludeDirs dirs = includeDirsOf(root);

    Closure closure;
    QSet<QString> seen;
    QQueue<QString> queue;
    queue.enqueue(QFileInfo(start).absoluteFilePath());
    seen.insert(queue.head());

    while (!queue.isEmpty())
    {
        const QString file = queue.dequeue();
        closure.filesVisited++;
        const QString relative = QDir(root).relativeFilePath(file);
        closure.visited << relative;
        const QString text = readText(file);
        for (const QString& cls : forwardDeclaredQtClasses(text))
            closure.qtIncludes << relative + ": forward declaration of " + cls;

        auto it = include_line.globalMatch(text);
        while (it.hasNext())
        {
            const QRegularExpressionMatch m = it.next();
            const bool angle = m.captured(1) == "<";
            const QString name = m.captured(2);

            if (angle)
            {
                if (isQtHeader(name))
                    closure.qtIncludes << relative + ": #include <" + name + ">";
                continue;
            }

            QString resolved;
            const QString beside = QFileInfo(file).absolutePath() + "/" + name;
            if (QFileInfo::exists(beside))
                resolved = beside;
            for (const QString& dir : dirs.core)
                if (resolved.isEmpty() && QFileInfo::exists(dir + "/" + name))
                    resolved = dir + "/" + name;
            if (resolved.isEmpty())
            {
                for (const QString& dir : dirs.forbidden)
                {
                    if (QFileInfo::exists(dir + "/" + name))
                    {
                        closure.layerViolations << relative + ": includes \"" + name + "\" (" +
                                                       QDir(root).relativeFilePath(dir) + ")";
                        resolved = dir + "/" + name;
                        break;
                    }
                }
            }
            if (resolved.isEmpty())
                continue;   // a system or generated header

            const QString canonical = QFileInfo(resolved).absoluteFilePath();
            if (!seen.contains(canonical))
            {
                seen.insert(canonical);
                queue.enqueue(canonical);
            }
        }
    }
    return closure;
}

// ---------------------------------------------------------------------------
// Where Qt may be outside the View (the end state of stage 1 of the Avalonia plan)
// ---------------------------------------------------------------------------

/// The files outside src/view and include/view that include a Qt header on purpose. Everything else
/// outside the View is core. This is the work still to do, as a list: each entry goes when its file loses
/// Qt, and a listed file that no longer includes Qt fails the test, so the list can only shrink.
const QStringList kQtOutsideTheView = {
    // The Qt application's entry point; the Avalonia shell replaces it at the switch-over.
    "src/main.cpp",
    // The four ViewModels: Qt objects (signals, properties) that C# rewrites in step 4.
    "src/viewmodel/batchcontroller.cpp", "src/viewmodel/mainviewmodel.cpp", "src/viewmodel/plotviewmodel.cpp",
    "src/viewmodel/processingcoordinator.cpp",
    "include/viewmodel/batchcontroller.h", "include/viewmodel/mainviewmodel.h", "include/viewmodel/plotviewmodel.h",
    "include/viewmodel/processingcoordinator.h",
    // The two serialisers of the UI's Qt DTOs (JSON through Qt); C# reads and writes template JSON itself.
    // Their headers forward-declare the Qt classes in their signatures rather than including them.
    "src/model/processingtemplateschema.cpp", "src/model/streamconfigschema.cpp",
    "include/model/processingtemplateschema.h", "include/model/streamconfigschema.h",
    // The UI-form DTOs: QString, QHash and QColor because the dialogs, the plot and the schemas speak them.
    // The core takes plain copies (StreamJobConfig, CsvSeries, ImportedSeries ...), so nothing in the core
    // reaches these; they are presentation types and step 4 rewrites them in C#.
    "include/dto/framesyncparams.h", "include/dto/plotseriesdata.h", "include/dto/processingtemplate.h",
    "include/dto/seriesappearance.h", "include/dto/source.h", "include/dto/streamconfig.h",
    // The Qt-side bridges and the plot's palette.
    "include/plotconstants.h", "include/qtbridge.h", "include/qtprocessingevents.h",
};

/// Every .cpp and .h under src/ and include/ outside the View, as project-relative paths.
QStringList filesOutsideTheView(const QString& root)
{
    QStringList files;
    for (const QString& top : { QStringLiteral("src"), QStringLiteral("include") })
    {
        QDirIterator it(root + "/" + top, {"*.cpp", "*.h"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            const QString relative = QDir(root).relativeFilePath(it.next());
            if (relative.startsWith("src/view/") || relative.startsWith("include/view/"))
                continue;
            files << relative;
        }
    }
    files.sort();
    return files;
}

/// The Qt @p path uses itself - headers it includes and classes it forward-declares - not through
/// anything else it includes.
QStringList directQtUses(const QString& path)
{
    static const QRegularExpression include_line(
        QStringLiteral("^[ \\t]*#[ \\t]*include[ \\t]*<([^>]+)>"), QRegularExpression::MultilineOption);
    const QString text = readText(path);
    QStringList found = forwardDeclaredQtClasses(text);
    auto it = include_line.globalMatch(text);
    while (it.hasNext())
    {
        const QString name = it.next().captured(1);
        if (isQtHeader(name))
            found << name;
    }
    return found;
}

/// What is wrong with the headers outside the View that are not on @p qtHeaders: Qt, or a ViewModel or
/// View header, anywhere in their include closure. @p checked, if given, is how many headers were looked at.
QStringList headerProblems(const QString& root, const QStringList& qtHeaders, int* checked = nullptr)
{
    QStringList problems;
    int count = 0;
    for (const QString& file : filesOutsideTheView(root))
    {
        if (!file.endsWith(".h") || qtHeaders.contains(file))
            continue;
        ++count;
        const Closure closure = walk(root + "/" + file, root);
        for (const QString& q : closure.qtIncludes)
            problems << file + " reaches Qt: " + q;
        for (const QString& v : closure.layerViolations)
            problems << file + " reaches up a layer: " + v;
    }
    if (checked)
        *checked = count;
    return problems;
}

/// Files outside the View that include Qt and are not in @p allowed, and listed files that do not.
void checkQtConfinement(const QString& root, const QStringList& allowed, QStringList& unlisted, QStringList& stale)
{
    const QStringList files = filesOutsideTheView(root);
    QSet<QString> using_qt;
    for (const QString& file : files)
        if (!directQtUses(root + "/" + file).isEmpty())
            using_qt.insert(file);
    for (const QString& file : files)
        if (using_qt.contains(file) && !allowed.contains(file))
            unlisted << file;
    for (const QString& file : allowed)
        if (!using_qt.contains(file))
            stale << file;
}

} // namespace

void TestCoreQtFree::theProcessingCoreIncludesNoQtHeader()
{
    const QString root = projectRoot();
    QStringList problems;
    for (const QString& name : kCoreSources)
    {
        const QString source = root + "/src/model/" + name + ".cpp";
        QVERIFY2(QFileInfo::exists(source), qPrintable(source));
        const Closure closure = walk(source, root);
        QVERIFY2(closure.filesVisited > 1, qPrintable(name + ": include walk found nothing to visit"));
        for (const QString& q : closure.qtIncludes)
            problems << name + ".cpp reaches  " + q;
    }
    problems.removeDuplicates();
    QVERIFY2(problems.isEmpty(),
             qPrintable("the processing core must compile with no Qt on the include path:\n  " +
                        problems.join("\n  ")));
}

void TestCoreQtFree::theProcessingCoreDoesNotReachIntoTheViewModelOrView()
{
    const QString root = projectRoot();
    QStringList problems;
    for (const QString& name : kCoreSources)
    {
        const Closure closure = walk(root + "/src/model/" + name + ".cpp", root);
        for (const QString& v : closure.layerViolations)
            problems << name + ".cpp: " + v;
    }
    problems.removeDuplicates();
    QVERIFY2(problems.isEmpty(), qPrintable("the core reaches up a layer:\n  " + problems.join("\n  ")));
}

void TestCoreQtFree::everyModelSourceIsClassifiedAsCoreOrUiSession()
{
    // A new file under src/model/ has to be put on one list or the other, which is a decision
    // somebody should make on purpose: either it joins the decode pipeline (and must be Qt-free) or it
    // is UI-session state that is allowed to be Qt.
    const QString root = projectRoot();
    const QStringList files = QDir(root + "/src/model").entryList({"*.cpp"}, QDir::Files, QDir::Name);
    QVERIFY2(files.size() >= kCoreSources.size() + kUiSessionSources.size(), "model sources not found");

    QStringList unclassified;
    QSet<QString> present;
    for (const QString& file : files)
    {
        const QString base = QFileInfo(file).completeBaseName();
        present.insert(base);
        if (!kCoreSources.contains(base) && !kUiSessionSources.contains(base))
            unclassified << file;
    }
    QVERIFY2(unclassified.isEmpty(),
             qPrintable("classify these in tst_coreqtfree.cpp (core, or UI-session):\n  " +
                        unclassified.join("\n  ")));

    // And a listed file that has gone is a stale list, not a pass.
    QStringList stale;
    for (const QString& name : kCoreSources + kUiSessionSources)
        if (!present.contains(name))
            stale << name;
    QVERIFY2(stale.isEmpty(), qPrintable("listed but no longer in src/model: " + stale.join(", ")));

    // The UI-session files are not an escape hatch for the core: none of them is a dependency of
    // a core file.
    for (const QString& name : kCoreSources)
    {
        const Closure closure = walk(root + "/src/model/" + name + ".cpp", root);
        for (const QString& ui : kUiSessionSources)
            for (const QString& visited : closure.visited)
                QVERIFY2(QFileInfo(visited).fileName() != ui + ".h", qPrintable(name + ".cpp depends on " + ui + ".h"));
    }
}

void TestCoreQtFree::theGuardSeesQtThroughSharedHeaders()
{
    // The guard is only worth having if it would catch the thing it is for: a Qt include buried in
    // a header that several core files share. Build a miniature tree with exactly that shape and
    // check the walker reports it, and that a clean tree reports nothing.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = dir.path();
    QVERIFY(QDir().mkpath(root + "/src/model"));
    QVERIFY(QDir().mkpath(root + "/include/model"));
    QVERIFY(QDir().mkpath(root + "/include/view"));

    auto write = [](const QString& path, const QString& text) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(text.toUtf8());
    };
    write(root + "/include/shared.h", "#include <string>\n#include \"inner.h\"\n");
    // Every spelling of a Qt header the guard must recognise, and one look-alike it must not.
    write(root + "/include/inner.h",
          "#include <QString>\n#include <QtGlobal>\n#include <QObject>\n#include <Quux.h>\nstruct X {};\n"
          "class QJsonObject;\nstruct QMetaObject;\nclass Quark;\n");
    write(root + "/include/model/widgety.h", "#include <vector>\n");
    write(root + "/include/view/panel.h", "#include <vector>\n");
    write(root + "/src/model/dirty.cpp", "#include \"shared.h\"\n");
    write(root + "/src/model/layered.cpp", "#include \"panel.h\"\n");
    write(root + "/src/model/clean.cpp", "#include <string>\n#include \"widgety.h\"\n// #include <QString> is only a comment's worth\n");

    const Closure dirty = walk(root + "/src/model/dirty.cpp", root);
    QCOMPARE(dirty.qtIncludes.size(), 5);
    QVERIFY2(dirty.qtIncludes.join(";").contains("inner.h: #include <QString>") &&
                 dirty.qtIncludes.join(";").contains("<QtGlobal>") &&
                 dirty.qtIncludes.join(";").contains("<QObject>") &&
                 dirty.qtIncludes.join(";").contains("forward declaration of QJsonObject") &&
                 dirty.qtIncludes.join(";").contains("forward declaration of QMetaObject") &&
                 !dirty.qtIncludes.join(";").contains("Quux") && !dirty.qtIncludes.join(";").contains("Quark"),
             qPrintable(dirty.qtIncludes.join(";")));

    const Closure layered = walk(root + "/src/model/layered.cpp", root);
    QCOMPARE(layered.layerViolations.size(), 1);
    QVERIFY(layered.qtIncludes.isEmpty());

    const Closure clean = walk(root + "/src/model/clean.cpp", root);
    QVERIFY2(clean.qtIncludes.isEmpty() && clean.layerViolations.isEmpty(),
             qPrintable((clean.qtIncludes + clean.layerViolations).join(";")));
}

void TestCoreQtFree::outsideTheViewQtIsConfinedToTheListedFiles()
{
    // The end state of stage 1: outside the View, Qt is in the ViewModels, the two serialisers, the UI-form
    // DTOs and the bridges - and nowhere else. A new file that includes Qt has to be put on the list on
    // purpose, and a listed file that has lost Qt has to come off it.
    const QString root = projectRoot();
    QStringList unlisted, stale;
    checkQtConfinement(root, kQtOutsideTheView, unlisted, stale);
    QVERIFY2(unlisted.isEmpty(),
             qPrintable("these include Qt but are not core and not on kQtOutsideTheView:\n  " + unlisted.join("\n  ")));
    QVERIFY2(stale.isEmpty(),
             qPrintable("listed in kQtOutsideTheView but they no longer include Qt - take them off:\n  " + stale.join("\n  ")));
}

void TestCoreQtFree::everyOtherHeaderIsQtFreeThroughAndThrough()
{
    // Direct uses are not enough: a header can reach Qt through a DTO it includes. Every header outside
    // the View that is not on the Qt list must have no Qt, and no ViewModel or View header, anywhere in its
    // include closure.
    const QString root = projectRoot();
    int checked = 0;
    const QStringList problems = headerProblems(root, kQtOutsideTheView, &checked);
    QVERIFY2(checked > 15, qPrintable(QString::number(checked)));   // the walk really did see the headers
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join("\n  ")));
}

void TestCoreQtFree::theConfinementGuardSeesAnUnlistedAndAStaleFile()
{
    // The guard is only worth having if it catches what it is for. A miniature tree: a ViewModel that is
    // allowed, a model file that sneaks Qt in, a View file (exempt), a clean file, and a listed file that
    // has lost its Qt.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = dir.path();
    for (const QString& sub : { "src/model", "src/view", "src/viewmodel", "include/dto", "include/view" })
        QVERIFY(QDir().mkpath(root + "/" + sub));
    auto write = [](const QString& path, const QString& text) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(text.toUtf8());
    };
    write(root + "/src/viewmodel/vm.cpp", "#include <QObject>\n");
    write(root + "/src/model/sneaky.cpp", "#include <string>\n#include <QString>\n");
    write(root + "/src/model/lookalike.cpp", "#include <Quux.h>\nclass Quark;\nclass QuxLike;\n");
    write(root + "/include/dto/fwd.h", "struct Plain;\nclass QJsonObject;\n");
    write(root + "/src/model/clean.cpp", "#include <string>\n// #include <QString> is only a comment's worth\n");
    write(root + "/src/view/dialog.cpp", "#include <QDialog>\n");
    write(root + "/include/view/dialog.h", "#include <QDialog>\n");
    write(root + "/include/dto/old.h", "#include <string>\n");
    // A header with no Qt in it that reaches up into the View: only the layer rule can object.
    write(root + "/include/view/panel.h", "#include <vector>\n");
    write(root + "/include/dto/uppity.h", "#include \"panel.h\"\n");

    QStringList unlisted, stale;
    checkQtConfinement(root, { "src/viewmodel/vm.cpp", "include/dto/old.h" }, unlisted, stale);
    QCOMPARE(unlisted, (QStringList{ "include/dto/fwd.h", "src/model/sneaky.cpp" }));
    QCOMPARE(stale, QStringList{ "include/dto/old.h" });

    // And the header check: uppity.h has no Qt but reaches the View; fwd.h forward-declares a Qt class.
    int checked = 0;
    const QStringList problems = headerProblems(root, {}, &checked);
    QCOMPARE(checked, 3);   // fwd.h, old.h and uppity.h (the View's own header is outside the check)
    QVERIFY2(problems.join(";").contains("include/dto/uppity.h reaches up a layer"), qPrintable(problems.join(";")));
    QVERIFY2(problems.join(";").contains("include/dto/fwd.h reaches Qt: include/dto/fwd.h: forward declaration of QJsonObject"),
             qPrintable(problems.join(";")));
    QVERIFY2(!problems.join(";").contains("old.h"), qPrintable(problems.join(";")));
}
