#include "app/Config.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <QProcessEnvironment>
#include <QString>

#ifdef Q_OS_MAC
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace {

#ifdef Q_OS_MAC
QString plistString(const char *key)
{
    CFStringRef cfKey = CFStringCreateWithCString(kCFAllocatorDefault, key, kCFStringEncodingUTF8);
    if (cfKey == nullptr) {
        qWarning() << "Config: CFStringCreateWithCString failed key=" << key;
        return {};
    }
    CFTypeRef value = CFBundleGetValueForInfoDictionaryKey(CFBundleGetMainBundle(), cfKey);
    CFRelease(cfKey);
    if (value == nullptr) {
        qInfo() << "Config: Info.plist missing key=" << key;
        return {};
    }
    if (CFGetTypeID(value) != CFStringGetTypeID()) {
        qWarning() << "Config: Info.plist key is not a string key=" << key;
        return {};
    }
    CFStringRef cfValue = static_cast<CFStringRef>(value);
    const CFIndex len = CFStringGetLength(cfValue);
    const CFIndex max = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
    QByteArray buf(static_cast<int>(max), '\0');
    if (!CFStringGetCString(cfValue, buf.data(), max, kCFStringEncodingUTF8)) {
        qWarning() << "Config: CFStringGetCString failed key=" << key;
        return {};
    }
    const QString out = QString::fromUtf8(buf.constData());
    qInfo() << "Config: Info.plist key=" << key << " chars=" << out.size();
    return out;
}
#else
QString plistString(const char *key)
{
    qWarning() << "Config: Info.plist read skipped, not macOS key=" << key;
    return {};
}
#endif

// ─── Ariadne's Thread [AT-0064] ─────────────────────
// What: Resolve config from env, then Info.plist via CFBundle, then fallback
// Why:  envOrPlist never read the bundle; Firebase keys in Info.plist were unused
// Date: 2026-08-25
// Related: [AT-0005] Config.h, packaging/macos/Info.plist
// ─────────────────────────────────────────────────────
QString envOrPlist(const char *envName, const char *plistKey, const QString &fallback)
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString fromEnv = env.value(QLatin1String(envName));
    if (!fromEnv.isEmpty()) {
        qInfo() << "Config: using env" << envName << " chars=" << fromEnv.size();
        return fromEnv;
    }
    const QByteArray fromGetenv = qgetenv(envName);
    if (!fromGetenv.isEmpty()) {
        qInfo() << "Config: using getenv" << envName << " chars=" << fromGetenv.size();
        return QString::fromUtf8(fromGetenv);
    }
    const QString fromPlist = plistString(plistKey);
    if (!fromPlist.isEmpty()) {
        qInfo() << "Config: using Info.plist" << plistKey << " chars=" << fromPlist.size();
        return fromPlist;
    }
    qWarning() << "Config: missing" << envName << "and" << plistKey << "using fallback chars=" << fallback.size();
    return fallback;
}

} // namespace

QString Config::apiBaseUrl()
{
    return envOrPlist("SEENSHOT_API_BASE", "apiBaseUrl", QStringLiteral("https://api.seenshot.com"));
}

QString Config::firebaseApiKey()
{
    return envOrPlist("SEENSHOT_FIREBASE_API_KEY", "firebaseApiKey", QString());
}

QString Config::firebaseProjectId()
{
    return envOrPlist("SEENSHOT_FIREBASE_PROJECT_ID", "firebaseProjectId", QString());
}

QString Config::firebaseAuthDomain()
{
    return envOrPlist("SEENSHOT_FIREBASE_AUTH_DOMAIN", "firebaseAuthDomain",
                      QStringLiteral("seenshot-app.firebaseapp.com"));
}

QString Config::firebaseAppId()
{
    return envOrPlist("SEENSHOT_FIREBASE_APP_ID", "firebaseAppId", QString());
}

QString Config::sparkleFeedUrl()
{
    return envOrPlist("SEENSHOT_SPARKLE_FEED", "sparkleFeedUrl",
                      QStringLiteral("https://updates.seenshot.com/appcast.xml"));
}

QString Config::googleOAuthClientId()
{
    return envOrPlist("SEENSHOT_GOOGLE_OAUTH_CLIENT_ID", "googleOAuthClientId", QString());
}

// ─── Ariadne's Thread [AT-0086] ─────────────────────
// What: Email-link continue URL is https:// + firebaseAuthDomain
// Why:  User web client authDomain is the authorized Identity Toolkit domain
// Date: 2026-08-25
// Related: [AT-0005] Config.h, [AT-0084] FirebaseAuthClient.cpp:sendEmailLink
// ─────────────────────────────────────────────────────
QString Config::emailLinkContinueUrl()
{
    const QString fromEnvOrPlist =
        envOrPlist("SEENSHOT_EMAIL_LINK_CONTINUE", "emailLinkContinueUrl", QString());
    if (!fromEnvOrPlist.isEmpty()) {
        return fromEnvOrPlist;
    }
    return QStringLiteral("https://") + firebaseAuthDomain() + QLatin1Char('/');
}

// ─── Ariadne's Thread [AT-0097] ─────────────────────
// What: Official Firebase Auth handler URL on authDomain
// Why:  Identity Toolkit createAuthUri continueUri must be an authorized Firebase domain
// Date: 2026-08-25
// Related: [AT-0005] Config.h, [AT-0089] FirebaseAuthClient.cpp:createAuthUri
// ─────────────────────────────────────────────────────
QString Config::firebaseAuthHandlerUrl()
{
    const QString url = QStringLiteral("https://") + firebaseAuthDomain() + QStringLiteral("/__/auth/handler");
    qInfo() << "Config: firebaseAuthHandlerUrl=" << url;
    return url;
}

// ─── Ariadne's Thread [AT-0101] ─────────────────────
// What: PostHog project key and host from env then Info.plist
// Why:  PRD-06 — same Config path as firebaseApiKey; empty key keeps the app alive
// Date: 2026-08-26
// Related: [AT-0005] Config.h, [AT-0102] Analytics.cpp:start, packaging/macos/Info.plist
// ─────────────────────────────────────────────────────
QString Config::posthogApiKey()
{
    return envOrPlist("SEENSHOT_POSTHOG_API_KEY", "posthogApiKey", QString());
}

QString Config::posthogHost()
{
    return envOrPlist("SEENSHOT_POSTHOG_HOST", "posthogHost", QStringLiteral("https://eu.i.posthog.com"));
}
