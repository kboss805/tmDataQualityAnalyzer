#include "tst_coreqtfree.h"

#include <QCoreApplication>
#include <QDir>
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
    "csvseriesparser", "csvserieswriter", "frameprocessor", "framesetup", "pipelinerunner",
    "seriescolumnschema", "stepdetector", "templatematcher", "textfile", "tomldocument",
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

        auto it = include_line.globalMatch(text);
        while (it.hasNext())
        {
            const QRegularExpressionMatch m = it.next();
            const bool angle = m.captured(1) == "<";
            const QString name = m.captured(2);

            if (angle)
            {
                // <QString>, <QtGlobal>, <QObject> ... but not a lower-case second letter such as
                // <Quux.h>: a Qt header is Q followed by a capital letter (or Qt itself).
                if (name.size() > 1 && name[0] == 'Q' && (name[1].isUpper() || name.startsWith("Qt")))
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
          "#include <QString>\n#include <QtGlobal>\n#include <QObject>\n#include <Quux.h>\nstruct X {};\n");
    write(root + "/include/model/widgety.h", "#include <vector>\n");
    write(root + "/include/view/panel.h", "#include <vector>\n");
    write(root + "/src/model/dirty.cpp", "#include \"shared.h\"\n");
    write(root + "/src/model/layered.cpp", "#include \"panel.h\"\n");
    write(root + "/src/model/clean.cpp", "#include <string>\n#include \"widgety.h\"\n// #include <QString> is only a comment's worth\n");

    const Closure dirty = walk(root + "/src/model/dirty.cpp", root);
    QCOMPARE(dirty.qtIncludes.size(), 3);
    QVERIFY2(dirty.qtIncludes.join(";").contains("inner.h: #include <QString>") &&
                 dirty.qtIncludes.join(";").contains("<QtGlobal>") &&
                 dirty.qtIncludes.join(";").contains("<QObject>") &&
                 !dirty.qtIncludes.join(";").contains("Quux"),
             qPrintable(dirty.qtIncludes.join(";")));

    const Closure layered = walk(root + "/src/model/layered.cpp", root);
    QCOMPARE(layered.layerViolations.size(), 1);
    QVERIFY(layered.qtIncludes.isEmpty());

    const Closure clean = walk(root + "/src/model/clean.cpp", root);
    QVERIFY2(clean.qtIncludes.isEmpty() && clean.layerViolations.isEmpty(),
             qPrintable((clean.qtIncludes + clean.layerViolations).join(";")));
}
