#include "app/Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QStandardPaths>
#include <QTextStream>

namespace {

constexpr qint64 kMaxLogBytes = 5 * 1024 * 1024;

QFile *gLogFile = nullptr;
QMutex gLogMutex;
QString gLogPath;

// ─── Ariadne's Thread [AT-0004] ─────────────────────
// What: Rotate seenshot.log when it exceeds 5 MB
// Why:  Unbounded logs fill the user disk
// Date: 2026-08-25
// Related: [AT-0003] Logger.h
// ─────────────────────────────────────────────────────
void rotateIfNeeded(QFile *file)
{
    if (!file || file->size() < kMaxLogBytes) {
        return;
    }
    file->close();
    const QString path = file->fileName();
    QFile::remove(path + QStringLiteral(".2"));
    QFile::rename(path + QStringLiteral(".1"), path + QStringLiteral(".2"));
    QFile::rename(path, path + QStringLiteral(".1"));
    if (!file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        fprintf(stderr, "Logger: rotate reopen failed\n");
    }
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QMutexLocker lock(&gLogMutex);
    const char *level = "INFO";
    switch (type) {
    case QtDebugMsg:
        level = "DEBUG";
        break;
    case QtInfoMsg:
        level = "INFO";
        break;
    case QtWarningMsg:
        level = "WARN";
        break;
    case QtCriticalMsg:
        level = "ERROR";
        break;
    case QtFatalMsg:
        level = "FATAL";
        break;
    }

    const QString line = QStringLiteral("%1 [%2] %3 (%4:%5)\n")
                             .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate),
                                  QLatin1String(level),
                                  msg,
                                  QLatin1String(context.file ? context.file : "?"),
                                  QString::number(context.line));

    fprintf(stderr, "%s", qPrintable(line));
    if (gLogFile && gLogFile->isOpen()) {
        rotateIfNeeded(gLogFile);
        QTextStream stream(gLogFile);
        stream << line;
        stream.flush();
    }
}

} // namespace

void Logger::install()
{
    const QString dirPath =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/logs");
    QDir().mkpath(dirPath);
    const QString path = dirPath + QStringLiteral("/seenshot.log");
    gLogPath = path;
    gLogFile = new QFile(path);
    if (!gLogFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        fprintf(stderr, "Logger: failed to open %s\n", qPrintable(path));
    } else {
        fprintf(stderr, "Logger: writing to %s\n", qPrintable(path));
    }
    qInstallMessageHandler(messageHandler);
    qInfo() << "Logger: installed file rotation 5MB x 3 at" << path;
}

QString Logger::filePath()
{
    qInfo() << "Logger: filePath chars=" << gLogPath.size();
    return gLogPath;
}
