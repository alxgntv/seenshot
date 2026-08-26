#include "app/Analytics.h"

#include "app/Config.h"
#include "app/Logger.h"

#include <QCoreApplication>
#include <QDebug>
#include <QMutexLocker>

#include <posthog/posthog.h>

Analytics &Analytics::instance()
{
    static Analytics g;
    return g;
}

// ─── Ariadne's Thread [AT-0102] ─────────────────────
// What: initialize, crash handler, setLogFile, then app_started
// Why:  Official posthog-cpp cycle; empty key must not call initialize
// Date: 2026-08-26
// Related: [AT-0103] main.cpp, [AT-0004] Logger.cpp, docs/PRD-06-analytics.md
// ─────────────────────────────────────────────────────
void Analytics::start()
{
    QMutexLocker lock(&m_mutex);
    if (m_started) {
        qInfo() << "Analytics: start skipped already started stopped=" << m_stopped;
        return;
    }
    m_started = true;
    const QString apiKey = Config::posthogApiKey();
    const QString host = Config::posthogHost();
    const QString version = QCoreApplication::applicationVersion();
    qInfo() << "Analytics: start apiKeyChars=" << apiKey.size() << " host=" << host
            << " version=" << version;
    if (apiKey.isEmpty()) {
        qWarning() << "Analytics: empty api key, initialize skipped";
        return;
    }
    PostHog::Config config;
    config.apiKey = apiKey.toStdString();
    config.appName = "SeenShot";
    config.appVersion = version.toStdString();
    config.host = host.toStdString();
    m_client = std::make_unique<PostHog::Client>(config);
    if (!m_client->initialize()) {
        qWarning() << "Analytics: initialize failed enabled=" << m_client->isEnabled();
        m_client.reset();
        return;
    }
    qInfo() << "Analytics: initialized enabled=" << m_client->isEnabled();
    m_client->installCrashHandler();
    const QString logPath = Logger::filePath();
    if (!logPath.isEmpty()) {
        m_client->setLogFile(logPath.toStdString(), 50);
        qInfo() << "Analytics: setLogFile chars=" << logPath.size();
    } else {
        qWarning() << "Analytics: setLogFile skipped empty Logger path";
    }
    m_client->setCrashMetadata({{"app_version", version.toStdString()}});
    lock.unlock();
    track(QStringLiteral("app_started"), {{QStringLiteral("version"), version}});
}

void Analytics::shutdown()
{
    PostHog::Client *client = nullptr;
    {
        QMutexLocker lock(&m_mutex);
        if (m_stopped) {
            qInfo() << "Analytics: shutdown already done";
            return;
        }
        m_stopped = true;
        client = m_client.get();
        qInfo() << "Analytics: shutdown hasClient=" << (client != nullptr);
    }
    if (client) {
        client->shutdown();
        qInfo() << "Analytics: client shutdown finished";
    }
}

void Analytics::track(const QString &event, const QMap<QString, QString> &properties)
{
    QMutexLocker lock(&m_mutex);
    const QStringList keys = properties.keys();
    qInfo() << "Analytics: track" << event << " keys=" << keys << " stopped=" << m_stopped
            << " hasClient=" << (m_client != nullptr);
    if (m_stopped || !m_client || !m_client->isEnabled()) {
        qInfo() << "Analytics: track skipped" << event;
        return;
    }
    nlohmann::json json = nlohmann::json::object();
    for (auto it = properties.constBegin(); it != properties.constEnd(); ++it) {
        json[it.key().toStdString()] = it.value().toStdString();
    }
    m_client->track(event.toStdString(), json);
    m_client->setCrashMetadata({{"last_action", event.toStdString()},
                                {"app_version", QCoreApplication::applicationVersion().toStdString()}});
}
