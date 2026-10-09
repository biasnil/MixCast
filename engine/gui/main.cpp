// MixCast GUI - entry point.
#include "app_paths.h"
#include "main_window.h"
#include "theme.h"

#include <QApplication>
#include <QMessageBox>
#include <QSettings>
#include <QStyleFactory>
#include <QSystemTrayIcon>

#ifdef _WIN32
#include <windows.h>
#endif

#ifndef MIXCAST_VERSION
#define MIXCAST_VERSION "dev"
#endif

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("MixCast"));
    QApplication::setApplicationName(QStringLiteral("MixCast"));
    QApplication::setApplicationVersion(QStringLiteral(MIXCAST_VERSION));

#ifdef _WIN32
    // One MixCast at a time. The installer also looks for this name so it can
    // ask you to close MixCast before updating or uninstalling.
    HANDLE instance = CreateMutexW(nullptr, TRUE, L"MixCastRunningMutex");
    if (instance && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        QMessageBox::information(nullptr, QStringLiteral("MixCast"),
                                 QStringLiteral("MixCast is already running. Look for its icon in the system tray."));
        return 0;
    }
#endif
    QApplication::setWindowIcon(theme::LogoIcon());

    // All settings go to %LOCALAPPDATA%\MixCast\MixCast.ini (moved from the registry if needed).
    SetupSettingsStorage();

    // Colours: the theme you picked last time (Ocean the first time).
    theme::SetTheme(QSettings().value(QStringLiteral("ui/theme"), theme::ThemeOcean).toInt());

    // Fusion gives the stylesheet a clean, predictable base on every Windows version.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication::setFont(theme::Font(10));
    app.setStyleSheet(theme::StyleSheet());

    // Closing the window keeps mixing in the tray; "Quit MixCast" exits.
    QApplication::setQuitOnLastWindowClosed(false);

    // "--minimized" (used by "Start with Windows") starts in the tray.
    const bool minimized = QApplication::arguments().contains(QStringLiteral("--minimized"));

    MainWindow w;
    if (!minimized || !QSystemTrayIcon::isSystemTrayAvailable()) w.show();
    return app.exec();
}
