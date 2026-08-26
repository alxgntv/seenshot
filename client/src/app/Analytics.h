#pragma once

#include <QMap>
#include <QMutex>
#include <QString>

#include <memory>

namespace PostHog {
class Client;
}

// ─── Ariadne's Thread [AT-0102] ─────────────────────
// What: One PostHog::Client for events and crash reports
// Why:  PRD-06 — two clients would install two signal handlers
// Date: 2026-08-26
// Related: [AT-0100] CMakeLists.txt, [AT-0101] Config.cpp:posthogApiKey, docs/PRD-06-analytics.md
// ─────────────────────────────────────────────────────
class Analytics {
public:
    static Analytics &instance();
    void start();
    void shutdown();
    void track(const QString &event, const QMap<QString, QString> &properties = {});

private:
    Analytics() = default;
    QMutex m_mutex;
    std::unique_ptr<PostHog::Client> m_client;
    bool m_started = false;
    bool m_stopped = false;
};
