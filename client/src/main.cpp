#include "app/Analytics.h"
#include "app/Application.h"
#include "app/Logger.h"
#include "local/LocalStore.h"
#include "update/SparkleUpdater.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QIcon>

// ─── Ariadne's Thread [AT-0103] ─────────────────────
// What: Start PostHog after Logger, shutdown after exec
// Why:  PRD-06 — crash handler needs the log file path; flush on exit
// Date: 2026-08-26
// Related: [AT-0102] Analytics.cpp:start, [AT-0004] Logger.cpp:install
// ─────────────────────────────────────────────────────
int main(int argc, char *argv[])
{
    QApplication::setApplicationName(QStringLiteral("SeenShot"));
    QApplication::setOrganizationName(QStringLiteral("SeenShot"));
    // ─── Ariadne's Thread [AT-0383] ─────────────────────
    // What: Settings and logs report 0.1.9
    // Why:  Sparkle and the GitHub tag must match CFBundleShortVersionString
    // Date: 2026-08-29
    // Related: [AT-0383] CMakeLists.txt, [AT-0383] packaging/macos/Info.plist
    // ─────────────────────────────────────────────────────
    QApplication::setApplicationVersion(QStringLiteral("0.1.9"));
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    const QString icns = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../Resources/SeenShot.icns"));
    const QIcon appIcon(icns);
    app.setWindowIcon(appIcon);
    Logger::install();
    qInfo() << "main: SeenShot starting Qt" << QT_VERSION_STR
            << "version=" << QApplication::applicationVersion()
            << "iconNull=" << appIcon.isNull() << "icns=" << icns;
    // ─── Ariadne's Thread [AT-0304] ─────────────────────
    // What: Honor --reset-onboarding before Application::start
    // Why:  Developer can re-run the 6-step wizard without wiping account or hotkeys
    // Date: 2026-08-28
    // Related: [AT-0301] LocalStore.cpp:resetOnboarding, [AT-0303] Application.cpp:start
    // ─────────────────────────────────────────────────────
    if (app.arguments().contains(QStringLiteral("--reset-onboarding"))) {
        qInfo() << "main: --reset-onboarding args=" << app.arguments().size();
        LocalStore::resetOnboarding();
    }
    Analytics::instance().start();
    SparkleUpdater::start();
    Application seenshot;
    seenshot.start();
    const int code = app.exec();
    Analytics::instance().shutdown();
    qInfo() << "main: exit" << code;
    return code;
}
