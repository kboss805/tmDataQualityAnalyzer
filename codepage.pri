# codepage.pri - run the application, and its tests, in the UTF-8 active code page.
#
# Chapter 10 files are opened by the vendored irig106 library, which calls the C
# runtime's narrow open(char*) - and lib/irig106 may not be edited. Without this the
# CRT reads that char* in the legacy ANSI code page, so a recording under a folder
# whose name the code page cannot spell (any non-Latin script on a Western system)
# could not be opened at all, and the two readers disagreed about the rest: the
# metadata reader encoded the path with toLocal8Bit() and the packet reader with
# toUtf8(), so a file under C:\Users\Jos<e-acute>\ listed its channels and then
# failed the moment processing started.
#
# The activeCodePage manifest setting (Windows 10 1903 and later) makes the process
# code page UTF-8: the CRT's narrow file functions accept UTF-8, and Qt's
# toLocal8Bit() produces it. Both readers therefore pass toLocal8Bit(), which stays
# correct on an older Windows that ignores the setting - there it is simply the ANSI
# code page again, as before, and the two readers at least agree.
#
# Shared by tmDataQualityAnalyzer.pro and tests/tests.pro so the two executables
# cannot drift: TestFrameProcessor::nonAsciiPathOpensInBothReaders runs a path
# outside every Western code page through both readers.

msvc {
    # Merged with the manifest the linker already embeds (/MANIFEST:embed), which
    # carries the Common Controls dependency; this adds the windowsSettings only.
    QMAKE_LFLAGS += /MANIFESTINPUT:$$shell_quote($$system_path($$PWD/resources/utf8codepage.manifest))
}
