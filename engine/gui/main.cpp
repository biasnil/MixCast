// MixCast GUI - entry point.
#include "app_paths.h"
#include "main_window.h"
#include "theme.h"

#include <QApplication>
#include <QStyleFactory>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("MixCast"));
    QApplication::setApplicationName(QStringLiteral("MixCast"));
    QApplication::setWindowIcon(theme::LogoIcon());

    // All settings go to %LOCALAPPDATA%\MixCast\MixCast.ini (moved from the registry if needed).
    SetupSettingsStorage();

    // Fusion gives the stylesheet a clean, predictable base on every Windows version.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication::setFont(theme::Font(10));
    app.setStyleSheet(theme::StyleSheet());

    // Closing the window keeps mixing in the tray; "Quit MixCast" exits.
    QApplication::setQuitOnLastWindowClosed(false);

    MainWindow w;
    w.show();
    return app.exec();
}
