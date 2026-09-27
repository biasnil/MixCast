// MixCast GUI - where MixCast keeps its files.
#include "app_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace {

QString LocalAppDataBase()
{
    QString base = qEnvironmentVariable("LOCALAPPDATA");   // C:\Users\<you>\AppData\Local
    if (base.isEmpty()) base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir::cleanPath(base);
}

QString Ensure(const QString& dir)
{
    QDir().mkpath(dir);
    return dir;
}

} // namespace

QString MixCastDataDir()   { return Ensure(LocalAppDataBase() + QStringLiteral("/MixCast")); }
QString MixCastSoundsDir() { return Ensure(MixCastDataDir() + QStringLiteral("/Sounds")); }

void SetupSettingsStorage()
{
    // QSettings(IniFormat, UserScope, "MixCast", "MixCast") ->
    //   <LOCALAPPDATA>/MixCast/MixCast.ini
    MixCastDataDir();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, LocalAppDataBase());

    // One-time move from the registry (HKCU\Software\MixCast) used by older builds.
    QSettings ini;
    if (!ini.allKeys().isEmpty()) return;

    QSettings reg(QSettings::NativeFormat, QSettings::UserScope,
                  QCoreApplication::organizationName(), QCoreApplication::applicationName());
    const QStringList keys = reg.allKeys();
    if (keys.isEmpty()) return;

    for (const QString& k : keys) ini.setValue(k, reg.value(k));
    ini.sync();
    if (ini.status() == QSettings::NoError) reg.clear();   // don't leave a second copy behind
}

bool IsStoredSound(const QString& path)
{
    const QString dir = QDir::cleanPath(MixCastSoundsDir()) + QLatin1Char('/');
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath()).startsWith(dir, Qt::CaseInsensitive);
}

QString StoreSoundFile(const QString& path)
{
    const QFileInfo src(path);
    if (!src.exists() || IsStoredSound(path)) return path;

    const QString dir  = MixCastSoundsDir();
    const QString base = src.completeBaseName();
    const QString ext  = src.suffix();

    QString dest = QStringLiteral("%1/%2.%3").arg(dir, base, ext);
    for (int n = 2; QFileInfo::exists(dest); n++)
    {
        // Same file added again: reuse the existing copy.
        if (QFileInfo(dest).size() == src.size()) return dest;
        dest = QStringLiteral("%1/%2 (%3).%4").arg(dir, base).arg(n).arg(ext);
    }
    return QFile::copy(path, dest) ? dest : path;
}
