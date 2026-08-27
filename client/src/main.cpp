#include "app/Analytics.h"
#include "app/Application.h"
#include "app/Logger.h"
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
    QApplication::setApplicationVersion(QStringLiteral("0.1.3"));
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    const QString icns = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../Resources/SeenShot.icns"));
    const QIcon appIcon(icns);
    app.setWindowIcon(appIcon);
    Logger::install();
    qInfo() << "main: SeenShot starting Qt" << QT_VERSION_STR
            << "iconNull=" << appIcon.isNull() << "icns=" << icns;
    Analytics::instance().start();
    SparkleUpdater::start();
    Application seenshot;
    seenshot.start();
    const int code = app.exec();
    Analytics::instance().shutdown();
    qInfo() << "main: exit" << code;
    return code;
}
