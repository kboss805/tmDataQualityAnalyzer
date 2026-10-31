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
    "calibrationextractor", "calibrationrequest", "ch10packetreader", "channeldata", "chapter10reader", "chartgeometry",
    "clocktime",
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
    for (const QString& top : { QStringLiteral("src"), QStringLiteral("include"), QStringLiteral("native") })
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

// ---------------------------------------------------------------------------
// The native project (native/tmdq_core) builds the same core the Qt build does
// ---------------------------------------------------------------------------

/// The MSBuild project that builds tmdq_core.dll. Its source list is a second copy of kCoreSources (and of the
/// .pro's irig106 files) that a compiler cannot cross-check, so the test does.
const char* const kNativeProject  = "native/tmdq_core/tmdq_core.vcxproj";
const char* const kNativeSolution = "tmdq.sln";

/// The files a project compiles: every `<ClCompile Include="...">`, backslashes read as slashes. An `Update=`
/// item changes the metadata of files already listed and is not a source.
QStringList projectCompiledFiles(const QString& vcxproj)
{
    static const QRegularExpression include(QStringLiteral("<ClCompile\\s+Include=\"([^\"]+)\""));
    QStringList files;
    QRegularExpressionMatchIterator it = include.globalMatch(vcxproj);
    while (it.hasNext())
        files << QString(it.next().captured(1)).replace('\\', '/');
    return files;
}

/// The names (no folder, no extension) of the sources a project compiles from @p folder, sorted.
QStringList compiledNamesIn(const QStringList& files, const QString& folder, const QString& extension)
{
    QStringList names;
    for (const QString& file : files)
        if (file.contains(folder) && file.endsWith(extension))
            names << QFileInfo(file).completeBaseName();
    names.sort();
    return names;
}

/// The vendored C sources the application's .pro compiles.
QStringList proIrigSources(const QString& pro)
{
    static const QRegularExpression source(QStringLiteral("lib/irig106/src/([A-Za-z0-9_]+)\\.c\\b"));
    QStringList names;
    QRegularExpressionMatchIterator it = source.globalMatch(pro);
    while (it.hasNext())
        names << it.next().captured(1);
    names.sort();
    return names;
}

/// A project file with its XML comments removed, so what is left is configuration.
QString withoutXmlComments(QString text)
{
    static const QRegularExpression comment(QStringLiteral("<!--.*?-->"), QRegularExpression::DotMatchesEverythingOption);
    return text.remove(comment);
}

QString fileText(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(f.readAll()) : QString();
}

/// What is wrong with the ABI sources under @p root: Qt, or a ViewModel or View header, anywhere in the
/// include closure of any `native/tmdq_core/*.cpp`, or a source whose walk found nothing to follow.
QStringList abiSourceProblems(const QString& root)
{
    QStringList problems;
    const QStringList sources = QDir(root + "/native/tmdq_core").entryList({"*.cpp"}, QDir::Files, QDir::Name);
    for (const QString& name : sources)
    {
        const Closure closure = walk(root + "/native/tmdq_core/" + name, root);
        if (closure.filesVisited <= 1)
            problems << name + ": include walk found nothing to visit";
        for (const QString& q : closure.qtIncludes)
            problems << name + " reaches " + q;
        for (const QString& v : closure.layerViolations)
            problems << name + ": " + v;
    }
    return problems;
}

/// What is wrong with the native project under @p root, compared with @p coreSources, the application's .pro
/// and the solution. Empty when the two builds compile the same core.
QStringList nativeProjectProblems(const QString& root, const QStringList& coreSources)
{
    QStringList problems;
    const QString project = fileText(root + "/" + kNativeProject);
    if (project.isEmpty())
        return { QString("cannot read ") + kNativeProject };
    const QStringList files = projectCompiledFiles(project);

    // The core: exactly the sources the Qt build treats as core.
    QStringList expected = coreSources;
    expected.sort();
    const QStringList compiled = compiledNamesIn(files, "/src/model/", ".cpp");
    for (const QString& name : expected)
        if (!compiled.contains(name))
            problems << "the project does not compile the core source " + name;
    for (const QString& name : compiled)
        if (!expected.contains(name))
            problems << "the project compiles " + name + ", which is not a core source";

    // The ABI's own sources: every .cpp in native/tmdq_core is built by the project and by the native tests (which
    // compile it in, so test_abi.cpp can call it as a host would), and neither builds anything that is not there.
    QStringList present;
    for (const QString& file : QDir(root + "/native/tmdq_core").entryList({"*.cpp"}, QDir::Files, QDir::Name))
        present << QFileInfo(file).completeBaseName();
    QStringList inProject;
    for (const QString& file : files)
        if (!file.contains('/') && file.endsWith(".cpp"))
            inProject << QFileInfo(file).completeBaseName();
    const QStringList inTests = compiledNamesIn(
        projectCompiledFiles(fileText(root + "/native/tmdq_core_tests/tmdq_core_tests.vcxproj")), "/tmdq_core/", ".cpp");
    for (const QString& name : present)
    {
        if (!inProject.contains(name))
            problems << "the project does not compile the ABI source " + name;
        if (!inTests.contains(name))
            problems << "the native tests do not compile the ABI source " + name;
    }
    for (const QString& name : inProject)
        if (!present.contains(name))
            problems << "the project compiles " + name + ".cpp, which is not in native/tmdq_core";
    for (const QString& name : inTests)
        if (!present.contains(name))
            problems << "the native tests compile " + name + ".cpp, which is not in native/tmdq_core";

    // Nothing from another layer ("/src/view" is also the prefix of "/src/viewmodel").
    for (const QString& file : files)
        if (file.contains("/src/view") || file.contains("/src/dto"))
            problems << "the project compiles a file from another layer: " + file;

    // The vendored library: the files the application compiles, and some of them.
    const QStringList irig = compiledNamesIn(files, "/lib/irig106/src/", ".c");
    const QStringList inPro = proIrigSources(fileText(root + "/tmDataQualityAnalyzer.pro"));
    if (irig.isEmpty() || irig != inPro)
        problems << "the project's irig106 sources (" + irig.join(",") + ") are not the .pro's (" + inPro.join(",") + ")";

    // No Qt in the configuration: outside the comments, the project never says the word.
    if (withoutXmlComments(project).contains("Qt", Qt::CaseInsensitive))
        problems << "the project mentions Qt outside its comments";

    // And it is part of the solution CI builds.
    if (!fileText(root + "/" + kNativeSolution).contains("native\\tmdq_core\\tmdq_core.vcxproj"))
        problems << QString(kNativeSolution) + " does not build the native project";
    return problems;
}


/// The folder and project of the native tests (plan step 2.4): doctest, built by MSBuild with no Qt.
const char* const kNativeTestsDir     = "native/tmdq_core_tests";
const char* const kNativeTestsProject = "native/tmdq_core_tests/tmdq_core_tests.vcxproj";

/// What is wrong with the native tests under @p root: a core source that is not core, or a project and a folder
/// that disagree about which test sources there are, a file from another layer, Qt in the configuration or in
/// anything a test source includes, or a solution that does not build it. Empty when the tests are Qt-free.
QStringList nativeTestsProblems(const QString& root, const QStringList& coreSources)
{
    QStringList problems;
    const QString project = fileText(root + "/" + kNativeTestsProject);
    if (project.isEmpty())
        return { QString("cannot read ") + kNativeTestsProject };
    const QStringList files = projectCompiledFiles(project);

    // The core it exercises: some of it, and all of it core. (It grows as suites are ported.)
    const QStringList compiled = compiledNamesIn(files, "/src/model/", ".cpp");
    if (compiled.isEmpty())
        problems << "the tests project compiles no core source";
    for (const QString& name : compiled)
        if (!coreSources.contains(name))
            problems << "the tests project compiles " + name + ", which is not a core source";

    // Its own sources: every .cpp in the folder is built, and nothing else is claimed.
    QStringList present;
    for (const QString& file : QDir(root + "/" + kNativeTestsDir).entryList({"*.cpp"}, QDir::Files, QDir::Name))
        present << QFileInfo(file).completeBaseName();
    QStringList inProject;
    for (const QString& file : files)
        if (!file.contains('/') && file.endsWith(".cpp"))
            inProject << QFileInfo(file).completeBaseName();
    for (const QString& name : present)
        if (!inProject.contains(name))
            problems << "the tests project does not compile " + name + ".cpp";
    for (const QString& name : inProject)
        if (!present.contains(name))
            problems << "the tests project compiles " + name + ".cpp, which is not in " + kNativeTestsDir;

    // Nothing from another layer, and the project never says Qt outside its comments.
    for (const QString& file : files)
        if (file.contains("/src/view") || file.contains("/src/dto"))
            problems << "the tests project compiles a file from another layer: " + file;
    if (withoutXmlComments(project).contains("Qt", Qt::CaseInsensitive))
        problems << "the tests project mentions Qt outside its comments";

    // Nothing a test source includes - directly or through a header of ours - is Qt, a ViewModel or a View.
    // (A source that includes only a third-party header, like doctest's main, has nothing of ours to follow.)
    for (const QString& name : present)
    {
        const Closure closure = walk(root + "/" + kNativeTestsDir + "/" + name + ".cpp", root);
        for (const QString& q : closure.qtIncludes)
            problems << name + ".cpp reaches " + q;
        for (const QString& v : closure.layerViolations)
            problems << name + ".cpp: " + v;
    }

    if (!fileText(root + "/" + kNativeSolution).contains("native\\tmdq_core_tests\\tmdq_core_tests.vcxproj"))
        problems << QString(kNativeSolution) + " does not build the native tests";
    return problems;
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
    for (const QString& sub : { "src/model", "src/view", "src/viewmodel", "include/dto", "include/view", "native/tmdq_core" })
        QVERIFY(QDir().mkpath(root + "/" + sub));
    auto write = [](const QString& path, const QString& text) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(text.toUtf8());
    };
    write(root + "/src/viewmodel/vm.cpp", "#include <QObject>\n");
    write(root + "/native/tmdq_core/abi_sneaky.cpp", "#include <QString>\n");
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
    QCOMPARE(unlisted, (QStringList{ "include/dto/fwd.h", "native/tmdq_core/abi_sneaky.cpp", "src/model/sneaky.cpp" }));
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

void TestCoreQtFree::theNativeProjectCompilesExactlyTheCoreSources()
{
    // tmdq_core.dll is built by MSBuild from its own source list, and the Qt application by qmake from
    // another. They are one set of files, and a new core source added to one build and not the other would
    // compile in the app and be missing from the DLL (or the reverse) with nothing to say so.
    const QStringList problems = nativeProjectProblems(projectRoot(), kCoreSources);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join("\n  ")));
}

void TestCoreQtFree::theNativeProjectGuardReadsProjectFilesCorrectly()
{
    // The readers: an Include in either slash style but not an Update; the right folder and extension; a
    // comment that mentions Qt is not configuration.
    const QString project =
        "<!-- built with no Qt, see TestCoreQtFree -->\n"
        "<ItemGroup>\n"
        "  <ClCompile Include=\"tmdq_core.cpp\" />\n"
        "  <ClCompile Include=\"..\\..\\src\\model\\alpha.cpp\" />\n"
        "  <ClCompile Include=\"../../src/model/beta.cpp\"/>\n"
        "  <ClCompile Include=\"..\\..\\src\\view\\gamma.cpp\" />\n"
        "  <ClCompile Include=\"..\\..\\lib\\irig106\\src\\i106_time.c\" />\n"
        "  <ClCompile Update=\"..\\..\\lib\\irig106\\src\\*.c\"><WarningLevel>Off</WarningLevel></ClCompile>\n"
        "  <ClCompile Include=\"..\\..\\src\\model\\notasource.h\" />\n"
        "</ItemGroup>\n";
    const QStringList files = projectCompiledFiles(project);
    QCOMPARE(files.size(), 6);
    QCOMPARE(compiledNamesIn(files, "/src/model/", ".cpp"), (QStringList{ "alpha", "beta" }));
    QCOMPARE(compiledNamesIn(files, "/lib/irig106/src/", ".c"), QStringList{ "i106_time" });
    QVERIFY(withoutXmlComments(project).contains("ClCompile"));
    QVERIFY(!withoutXmlComments(project).contains("Qt"));
    QVERIFY(project.contains("Qt"));

    // The .pro's list: only the C sources under lib/irig106/src, names sorted, headers and comments not counted.
    const QString pro =
        "# Compile the vendored third-party lib (lib/irig106) without warnings\n"
        "SOURCES += \\\n    lib/irig106/src/zeta.c \\\n    lib/irig106/src/i106_time.c \\\n    lib/irig106/src/notc.cc \\\n"
        "HEADERS += lib/irig106/include/irig106ch10.h\n";
    QCOMPARE(proIrigSources(pro), (QStringList{ "i106_time", "zeta" }));
}

void TestCoreQtFree::theNativeProjectGuardSeesEveryKindOfDrift()
{
    // The check itself, on a miniature tree: one that agrees, then one fault at a time.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = dir.path();
    QVERIFY(QDir().mkpath(root + "/native/tmdq_core"));
    QVERIFY(QDir().mkpath(root + "/native/tmdq_core_tests"));
    auto write = [](const QString& path, const QString& text) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(text.toUtf8());
    };
    write(root + "/native/tmdq_core/tmdq_core.cpp", "");
    write(root + "/native/tmdq_core/abi_extra.cpp", "");
    write(root + "/native/tmdq_core/notes.txt", "not a source");
    // The native tests' project, compiling the ABI in beside a test source and a core source of its own.
    auto testsProject = [](const QStringList& abi) {
        QString text = "<Project>\n  <ClCompile Include=\"test_abi.cpp\" />\n"
                       "  <ClCompile Include=\"..\\..\\src\\model\\alpha.cpp\" />\n";
        for (const QString& name : abi)
            text += "  <ClCompile Include=\"..\\tmdq_core\\" + name + "\" />\n";
        return text + "</Project>\n";
    };
    const QString testsGood = testsProject({ "tmdq_core.cpp", "abi_extra.cpp" });
    const QStringList core = { "alpha", "beta" };
    const QString pro = "SOURCES += lib/irig106/src/one.c \\\n    lib/irig106/src/two.c \\\n";
    const QString solution = "Project(\"{x}\") = \"tmdq_core\", \"native\\tmdq_core\\tmdq_core.vcxproj\", \"{y}\"\n";
    auto project = [](const QStringList& items, const QString& config = "<RuntimeLibrary>MultiThreadedDLL</RuntimeLibrary>") {
        QString text = "<!-- no Qt here -->\n<Project>\n  " + config + "\n";
        for (const QString& item : items)
            text += "  <ClCompile Include=\"" + item + "\" />\n";
        return text + "</Project>\n";
    };
    const QStringList good = { "tmdq_core.cpp", "abi_extra.cpp", "..\\..\\src\\model\\alpha.cpp", "..\\..\\src\\model\\beta.cpp",
                               "..\\..\\lib\\irig106\\src\\one.c", "..\\..\\lib\\irig106\\src\\two.c" };
    auto check = [&](const QString& proj, const QString& proText, const QString& slnText,
                     const QString& testsText = QString()) {
        write(root + "/native/tmdq_core/tmdq_core.vcxproj", proj);
        write(root + "/tmDataQualityAnalyzer.pro", proText);
        write(root + "/tmdq.sln", slnText);
        write(root + "/native/tmdq_core_tests/tmdq_core_tests.vcxproj", testsText.isNull() ? testsGood : testsText);
        return nativeProjectProblems(root, core);
    };
    auto only = [](const QStringList& problems, const QString& needle) {
        return problems.size() == 1 && problems.first().contains(needle);
    };

    QStringList clean = check(project(good), pro, solution);
    QVERIFY2(clean.isEmpty(), qPrintable(clean.join(";")));

    QStringList missing = good;
    missing.removeAt(3);   // beta
    QStringList p = check(project(missing), pro, solution);
    QVERIFY2(only(p, "does not compile the core source beta"), qPrintable(p.join(";")));

    p = check(project(good + QStringList{ "..\\..\\src\\model\\gamma.cpp" }), pro, solution);
    QVERIFY2(only(p, "compiles gamma, which is not a core source"), qPrintable(p.join(";")));

    p = check(project(good + QStringList{ "..\\..\\src\\viewmodel\\vm.cpp" }), pro, solution);
    QVERIFY2(p.join(";").contains("a file from another layer: ../../src/viewmodel/vm.cpp"), qPrintable(p.join(";")));
    p = check(project(good + QStringList{ "..\\..\\src\\view\\v.cpp" }), pro, solution);
    QVERIFY2(p.join(";").contains("another layer: ../../src/view/v.cpp"), qPrintable(p.join(";")));
    p = check(project(good + QStringList{ "..\\..\\src\\dto\\d.cpp" }), pro, solution);
    QVERIFY2(p.join(";").contains("another layer: ../../src/dto/d.cpp"), qPrintable(p.join(";")));

    // The vendored library: one missing, one extra, and none at all (where the .pro has none either).
    QStringList noTwo = good;
    noTwo.removeLast();
    p = check(project(noTwo), pro, solution);
    QVERIFY2(only(p, "irig106 sources (one) are not the .pro's (one,two)"), qPrintable(p.join(";")));
    p = check(project(good + QStringList{ "..\\..\\lib\\irig106\\src\\three.c" }), pro, solution);
    QVERIFY2(only(p, "irig106 sources (one,three,two) are not the .pro's (one,two)"), qPrintable(p.join(";")));
    p = check(project({ "tmdq_core.cpp", "abi_extra.cpp", "..\\..\\src\\model\\alpha.cpp", "..\\..\\src\\model\\beta.cpp" }), "SOURCES += a.cpp\n", solution);
    QVERIFY2(only(p, "irig106 sources () are not the .pro's ()"), qPrintable(p.join(";")));

    // The ABI's own sources: one the project forgets, one it invents, and the same for the native tests.
    QStringList noAbiExtra = good;
    noAbiExtra.removeAt(1);
    p = check(project(noAbiExtra), pro, solution);
    QVERIFY2(only(p, "the project does not compile the ABI source abi_extra"), qPrintable(p.join(";")));
    p = check(project(good + QStringList{ "ghost.cpp" }), pro, solution);
    QVERIFY2(only(p, "the project compiles ghost.cpp, which is not in native/tmdq_core"), qPrintable(p.join(";")));
    p = check(project(good), pro, solution, testsProject({ "tmdq_core.cpp" }));
    QVERIFY2(only(p, "the native tests do not compile the ABI source abi_extra"), qPrintable(p.join(";")));
    p = check(project(good), pro, solution, testsProject({ "tmdq_core.cpp", "abi_extra.cpp", "ghost.cpp" }));
    QVERIFY2(only(p, "the native tests compile ghost.cpp, which is not in native/tmdq_core"), qPrintable(p.join(";")));
    p = check(project(good), pro, solution, "<Project />\n");
    QVERIFY2(p.size() == 2 && p.join(";").contains("tmdq_core") && p.join(";").contains("abi_extra"), qPrintable(p.join(";")));
    // The tests' own sources and the core sources they compile are not ABI sources.
    p = check(project(good), pro, solution, testsGood.left(testsGood.size() - 11) + "  <ClCompile Include=\"helper.cpp\" />\n</Project>\n");
    QVERIFY2(p.isEmpty(), qPrintable(p.join(";")));

    // Qt in the configuration, in any case; a comment does not count.
    p = check(project(good, "<AdditionalDependencies>qt6core.lib</AdditionalDependencies>"), pro, solution);
    QVERIFY2(only(p, "mentions Qt outside its comments"), qPrintable(p.join(";")));
    p = check(project(good, "<PreprocessorDefinitions>QT_CORE_LIB</PreprocessorDefinitions>"), pro, solution);
    QVERIFY2(only(p, "mentions Qt outside its comments"), qPrintable(p.join(";")));
    p = check(project(good, "<AdditionalIncludeDirectories>C:\\Qt\\6.12.0\\include</AdditionalIncludeDirectories>"), pro, solution);
    QVERIFY2(only(p, "mentions Qt outside its comments"), qPrintable(p.join(";")));

    // The solution.
    p = check(project(good), pro, "Project(\"{x}\") = \"other\", \"other.vcxproj\", \"{y}\"\n");
    QVERIFY2(only(p, "tmdq.sln does not build the native project"), qPrintable(p.join(";")));

    // And a project that is not there says so rather than passing.
    QVERIFY(QFile::remove(root + "/native/tmdq_core/tmdq_core.vcxproj"));
    p = nativeProjectProblems(root, core);
    QVERIFY2(only(p, "cannot read"), qPrintable(p.join(";")));
}

void TestCoreQtFree::theAbiSourcesIncludeNoQtAndReachNoHigherLayer()
{
    // The C ABI is the one door into the core, so it is held to the core's own rule: nothing it includes,
    // directly or through a header, is Qt, a ViewModel or a View.
    const QString root = projectRoot();
    QVERIFY2(QDir(root + "/native/tmdq_core").entryList({"*.cpp"}, QDir::Files).size() >= 3, "the ABI sources are not where expected");
    const QStringList problems = abiSourceProblems(root);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join("\n  ")));
}

void TestCoreQtFree::theAbiGuardSeesQtAndHigherLayers()
{
    // The check above, on a miniature tree: a source that includes Qt, one that forward-declares it through a
    // header, one that reaches the View, one with nothing to follow, and one that is clean.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = dir.path();
    QVERIFY(QDir().mkpath(root + "/native/tmdq_core"));
    QVERIFY(QDir().mkpath(root + "/include/view"));
    auto write = [](const QString& path, const QString& text) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(text.toUtf8());
    };
    write(root + "/include/view/panel.h", "#include <vector>\n");
    write(root + "/native/tmdq_core/shared.h", "#include <string>\n");
    write(root + "/native/tmdq_core/leaky.h", "class QObject;\n");
    write(root + "/native/tmdq_core/clean.cpp", "#include \"shared.h\"\n");
    write(root + "/native/tmdq_core/qt.cpp", "#include \"shared.h\"\n#include <QString>\n");
    write(root + "/native/tmdq_core/fwd.cpp", "#include \"leaky.h\"\n");
    write(root + "/native/tmdq_core/up.cpp", "#include \"shared.h\"\n#include \"panel.h\"\n");
    write(root + "/native/tmdq_core/alone.cpp", "// includes nothing of ours\n#include <vector>\n");

    const QStringList problems = abiSourceProblems(root);
    const QString all = problems.join("\n");
    QVERIFY2(all.contains("qt.cpp reaches native/tmdq_core/qt.cpp: #include <QString>"), qPrintable(all));
    QVERIFY2(all.contains("fwd.cpp reaches native/tmdq_core/leaky.h: forward declaration of QObject"), qPrintable(all));
    QVERIFY2(all.contains("up.cpp: native/tmdq_core/up.cpp: includes \"panel.h\""), qPrintable(all));
    QVERIFY2(all.contains("alone.cpp: include walk found nothing to visit"), qPrintable(all));
    QVERIFY2(!all.contains("clean.cpp"), qPrintable(all));
    QCOMPARE(problems.size(), 4);
}

void TestCoreQtFree::theNativeTestsAreQtFreeAndCompileOnlyCoreSources()
{
    // The doctest executable (plan step 2.4) is built by MSBuild with no Qt on any path, so a Qt include in a test
    // would fail its build; this says so here too, in the Qt suite, where a Qt-less CI would not be looking.
    const QString root = projectRoot();
    QVERIFY2(QDir(root + "/" + kNativeTestsDir).entryList({"test_*.cpp"}, QDir::Files).size() >= 1, "the native tests are not where expected");
    const QStringList problems = nativeTestsProblems(root, kCoreSources);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join("\n  ")));
}

void TestCoreQtFree::theNativeTestsGuardSeesEveryKindOfDrift()
{
    // The check above, on a miniature tree: one that agrees, then one fault at a time.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = dir.path();
    QVERIFY(QDir().mkpath(root + "/native/tmdq_core_tests"));
    QVERIFY(QDir().mkpath(root + "/include/view"));
    auto write = [](const QString& path, const QString& text) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(text.toUtf8());
    };
    write(root + "/include/view/panel.h", "#include <vector>\n");
    write(root + "/include/shared.h", "#include <string>\n");
    write(root + "/native/tmdq_core_tests/main.cpp", "#include \"doctest.h\"\n");
    write(root + "/native/tmdq_core_tests/test_a.cpp", "#include \"doctest.h\"\n#include \"shared.h\"\n");
    const QStringList core = { "alpha", "beta" };
    const QString solution = "Project(\"{x}\") = \"t\", \"native\\tmdq_core_tests\\tmdq_core_tests.vcxproj\", \"{y}\"\n";
    auto project = [](const QStringList& items, const QString& config = "<RuntimeLibrary>MultiThreadedDLL</RuntimeLibrary>") {
        QString text = "<!-- no Qt here -->\n<Project>\n  " + config + "\n";
        for (const QString& item : items)
            text += "  <ClCompile Include=\"" + item + "\" />\n";
        return text + "</Project>\n";
    };
    const QStringList good = { "main.cpp", "test_a.cpp", "..\\..\\src\\model\\alpha.cpp" };
    auto check = [&](const QString& proj, const QString& slnText) {
        write(root + "/native/tmdq_core_tests/tmdq_core_tests.vcxproj", proj);
        write(root + "/tmdq.sln", slnText);
        return nativeTestsProblems(root, core);
    };
    auto only = [](const QStringList& problems, const QString& needle) {
        return problems.size() == 1 && problems.first().contains(needle);
    };

    QStringList clean = check(project(good), solution);
    QVERIFY2(clean.isEmpty(), qPrintable(clean.join(";")));

    QStringList notCore = good;
    notCore << "..\\..\\src\\model\\gamma.cpp";
    QStringList p = check(project(notCore), solution);
    QVERIFY2(only(p, "gamma, which is not a core source"), qPrintable(p.join(";")));

    QStringList noCore = { "main.cpp", "test_a.cpp" };
    p = check(project(noCore), solution);
    QVERIFY2(only(p, "compiles no core source"), qPrintable(p.join(";")));

    QStringList unlisted = { "main.cpp", "..\\..\\src\\model\\alpha.cpp" };
    p = check(project(unlisted), solution);
    QVERIFY2(only(p, "does not compile test_a.cpp"), qPrintable(p.join(";")));

    QStringList ghost = good;
    ghost << "test_b.cpp";
    p = check(project(ghost), solution);
    QVERIFY2(only(p, "test_b.cpp, which is not in"), qPrintable(p.join(";")));

    QStringList view = good;
    view << "..\\..\\src\\view\\widget.cpp";
    p = check(project(view), solution);
    QVERIFY2(p.join(";").contains("a file from another layer: ../../src/view/widget.cpp"), qPrintable(p.join(";")));

    p = check(project(good, "<IncludePath>C:\\Qt\\6.12.0\\include</IncludePath>"), solution);
    QVERIFY2(only(p, "mentions Qt outside its comments"), qPrintable(p.join(";")));

    p = check(project(good), "Project(\"{x}\") = \"t\", \"native\\other.vcxproj\", \"{y}\"\n");
    QVERIFY2(only(p, "tmdq.sln does not build the native tests"), qPrintable(p.join(";")));

    // And what a test source includes: Qt directly, Qt behind a header of ours, the View behind another.
    write(root + "/native/tmdq_core_tests/test_a.cpp", "#include \"doctest.h\"\n#include <QString>\n");
    p = check(project(good), solution);
    QVERIFY2(only(p, "test_a.cpp reaches native/tmdq_core_tests/test_a.cpp: #include <QString>"), qPrintable(p.join(";")));

    write(root + "/include/leaky.h", "class QObject;\n");
    write(root + "/native/tmdq_core_tests/test_a.cpp", "#include \"leaky.h\"\n");
    p = check(project(good), solution);
    QVERIFY2(only(p, "test_a.cpp reaches include/leaky.h: forward declaration of QObject"), qPrintable(p.join(";")));

    write(root + "/native/tmdq_core_tests/test_a.cpp", "#include \"shared.h\"\n#include \"panel.h\"\n");
    p = check(project(good), solution);
    QVERIFY2(only(p, "test_a.cpp: native/tmdq_core_tests/test_a.cpp: includes \"panel.h\""), qPrintable(p.join(";")));

    QVERIFY(QFile::remove(root + "/native/tmdq_core_tests/tmdq_core_tests.vcxproj"));
    write(root + "/tmdq.sln", solution);
    p = nativeTestsProblems(root, core);
    QVERIFY2(only(p, "cannot read"), qPrintable(p.join(";")));
}
