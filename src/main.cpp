/**
 * @file main.cpp
 * @brief Application entry point — loads theme, creates MainView, and runs the event loop.
 */

#include "mainview.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QSettings>
#include <QStyleFactory>

#include "constants.h"
#include "tomlconfighelper.h"

#ifndef QT_NO_DEBUG
#include <intrin.h>
static void debugBreakOnFatal(QtMsgType type, const QMessageLogContext&, const QString&)
{
    if (type == QtFatalMsg)
        __debugbreak();
}
#endif
int main(int argc, char** argv)
{
    QApplication a(argc, argv);
    QCoreApplication::setOrganizationName(UIConstants::kOrganizationName);
    QCoreApplication::setApplicationName(UIConstants::kApplicationName);

    // Register the custom TOML format so all QSettings(path, TomlConfigHelper::format())
    // calls work correctly throughout the application lifetime.
    (void)TomlConfigHelper::format();

    // App-level preferences (theme, recent files, etc.) still use the platform
    // default format (INI on Windows via QSettings::IniFormat).
    QSettings::setDefaultFormat(QSettings::IniFormat);

    // Portable mode: if a "portable" marker file exists next to the executable,
    // redirect QSettings storage to the application directory instead of %APPDATA%.
    QString exe_dir = QCoreApplication::applicationDirPath();
    if (QFileInfo::exists(exe_dir + "/" + UIConstants::kPortableMarkerFilename))
    {
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, exe_dir);
    }

    // Force Fusion as the base style so QSS renders consistently across all
    // Windows versions. Without this, the platform default (e.g. "Windows"
    // on older PCs) produces mismatched scrollbar sizes and missing colors.
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    QSettings app_settings;
    QString theme = app_settings.value(UIConstants::kSettingsKeyTheme, UIConstants::kThemeDark).toString();
    QString qss_path = (theme == UIConstants::kThemeLight)
        ? ":/resources/win11-light.qss"
        : ":/resources/win11-dark.qss";

    QFile qss_file(qss_path);
    if (qss_file.open(QFile::ReadOnly))
    {
        QString styleSheet = QLatin1String(qss_file.readAll());
        qApp->setStyleSheet(styleSheet);
        qss_file.close();
    }

    QApplication::setWindowIcon(QIcon(":/resources/icon.ico"));

    MainView w;
    w.show();
#ifndef QT_NO_DEBUG
    qInstallMessageHandler(debugBreakOnFatal);
#endif
    return QApplication::exec();
}
