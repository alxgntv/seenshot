#include "auth/AuthSession.h"

#include "app/Analytics.h"

#include "app/Config.h"
#include "auth/ISecureStore.h"
#include "auth/MacGoogleAuth.h"
#include "local/LocalStore.h"

#include <QDateTime>
#include <QDebug>
#include <QNetworkInformation>
#include <QRegularExpression>
#include <QThread>
#include <QUrlQuery>

namespace {
constexpr auto kRefreshKey = "refresh_token";
constexpr auto kUidKey = "uid";
constexpr auto kEmailKey = "email";
} // namespace

AuthSession::AuthSession(ISecureStore *store, QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_firebase(nam)
{
    loadFromStore();
    qInfo() << "AuthSession: constructed hasSession=" << hasSession() << " emailChars=" << email().size();
}

bool AuthSession::loadFromStore()
{
    QString error;
    const QByteArray refresh = m_store->read(QString::fromLatin1(kRefreshKey), &error);
    const QByteArray uid = m_store->read(QString::fromLatin1(kUidKey), &error);
    const QByteArray email = m_store->read(QString::fromLatin1(kEmailKey), &error);
    if (refresh.isEmpty()) {
        qInfo() << "AuthSession: no stored refresh token";
        return false;
    }
    m_tokens.refreshToken = QString::fromUtf8(refresh);
    m_tokens.uid = QString::fromUtf8(uid);
    m_tokens.email = QString::fromUtf8(email);
    m_tokens.idToken.clear();
    m_tokens.expiresAtMs = 0;
    qInfo() << "AuthSession: loaded refresh token uid=" << m_tokens.uid << " emailChars=" << m_tokens.email.size();
    return true;
}

bool AuthSession::persist(const FirebaseTokens &tokens, QString *errorCode)
{
    if (!m_store->write(QString::fromLatin1(kRefreshKey), tokens.refreshToken.toUtf8(), errorCode)) {
        qWarning() << "AuthSession: persist refresh failed";
        return false;
    }
    if (!m_store->write(QString::fromLatin1(kUidKey), tokens.uid.toUtf8(), errorCode)) {
        qWarning() << "AuthSession: persist uid failed";
        return false;
    }
    if (!m_store->write(QString::fromLatin1(kEmailKey), tokens.email.toUtf8(), errorCode)) {
        qWarning() << "AuthSession: persist email failed";
        return false;
    }
    return true;
}

bool AuthSession::hasSession() const
{
    QMutexLocker lock(&m_mutex);
    return !m_tokens.refreshToken.isEmpty();
}

bool AuthSession::isOnline() const
{
    auto *info = QNetworkInformation::instance();
    if (!info) {
        qInfo() << "AuthSession: QNetworkInformation unavailable, assume online";
        return true;
    }
    const bool online = info->reachability() != QNetworkInformation::Reachability::Disconnected;
    qInfo() << "AuthSession: reachability online=" << online;
    return online;
}

QString AuthSession::uid() const
{
    QMutexLocker lock(&m_mutex);
    return m_tokens.uid;
}

QString AuthSession::email() const
{
    QMutexLocker lock(&m_mutex);
    return m_tokens.email;
}

bool AuthSession::beginAuth(QString *errorCode)
{
    QMutexLocker lock(&m_mutex);
    if (m_authBusy) {
        qWarning() << "AuthSession: auth already in flight";
        if (errorCode) {
            *errorCode = QStringLiteral("AUTH_IN_PROGRESS");
        }
        return false;
    }
    m_authBusy = true;
    return true;
}

void AuthSession::endAuth()
{
    QMutexLocker lock(&m_mutex);
    m_authBusy = false;
}

bool AuthSession::fillEmailIfNeeded(FirebaseTokens *tokens, QString *errorCode)
{
    if (!tokens->email.isEmpty()) {
        return true;
    }
    if (tokens->idToken.isEmpty()) {
        return true;
    }
    QString looked;
    if (!m_firebase.lookupEmail(tokens->idToken, &looked, errorCode)) {
        qWarning() << "AuthSession: lookup email failed, keep session";
        return true;
    }
    tokens->email = looked;
    return true;
}

// ─── Ariadne's Thread [AT-0104] ─────────────────────
// What: Track sign_in after persist with caller method
// Why:  PRD-06 — no email/uid in PostHog properties
// Date: 2026-08-26
// Related: [AT-0102] Analytics.cpp:track, [AT-0018] AuthSession.h
// ─────────────────────────────────────────────────────
bool AuthSession::finishTokens(const FirebaseTokens &tokens, const QString &method, QString *errorCode)
{
    FirebaseTokens filled = tokens;
    fillEmailIfNeeded(&filled, errorCode);
    {
        QMutexLocker lock(&m_mutex);
        m_signedOut = false;
        m_tokens = filled;
        if (!persist(filled, errorCode)) {
            return false;
        }
    }
    LocalStore::clearPendingSignInEmail();
    qInfo() << "AuthSession: session persisted uid=" << filled.uid << " emailChars=" << filled.email.size()
            << " method=" << method;
    Analytics::instance().track(QStringLiteral("sign_in"), {{QStringLiteral("method"), method}});
    emit sessionChanged();
    return true;
}

bool AuthSession::signInEmail(const QString &email, const QString &password, QString *errorCode)
{
    qInfo() << "AuthSession: signInEmail";
    if (!beginAuth(errorCode)) {
        return false;
    }
    FirebaseTokens tokens;
    const bool ok = m_firebase.signInEmail(email, password, &tokens, errorCode);
    if (!ok) {
        endAuth();
        return false;
    }
    const bool saved = finishTokens(tokens, QStringLiteral("password"), errorCode);
    endAuth();
    return saved;
}

bool AuthSession::signUpEmail(const QString &email, const QString &password, QString *errorCode)
{
    qInfo() << "AuthSession: signUpEmail";
    if (!beginAuth(errorCode)) {
        return false;
    }
    FirebaseTokens tokens;
    const bool ok = m_firebase.signUpEmail(email, password, &tokens, errorCode);
    if (!ok) {
        endAuth();
        return false;
    }
    const bool saved = finishTokens(tokens, QStringLiteral("signup"), errorCode);
    endAuth();
    return saved;
}

bool AuthSession::sendPasswordReset(const QString &email, QString *errorCode)
{
    qInfo() << "AuthSession: sendPasswordReset";
    if (!beginAuth(errorCode)) {
        return false;
    }
    const bool ok = m_firebase.sendPasswordReset(email, errorCode);
    endAuth();
    return ok;
}

bool AuthSession::sendEmailLink(const QString &email, QString *errorCode)
{
    qInfo() << "AuthSession: sendEmailLink";
    if (!beginAuth(errorCode)) {
        return false;
    }
    const bool ok = m_firebase.sendEmailLink(email, errorCode);
    if (ok) {
        LocalStore::setPendingSignInEmail(email);
        qInfo() << "AuthSession: pendingSignInEmail stored";
    }
    endAuth();
    return ok;
}

QString AuthSession::oobCodeFromUrl(const QUrl &url)
{
    QUrlQuery query(url);
    QString code = query.queryItemValue(QStringLiteral("oobCode"), QUrl::FullyDecoded);
    if (!code.isEmpty()) {
        return code;
    }
    const QString link = query.queryItemValue(QStringLiteral("link"), QUrl::FullyDecoded);
    if (!link.isEmpty()) {
        const QUrl inner(link);
        QUrlQuery innerQuery(inner);
        code = innerQuery.queryItemValue(QStringLiteral("oobCode"), QUrl::FullyDecoded);
        if (code.isEmpty()) {
            const QRegularExpression re(QStringLiteral("[?&]oobCode=([^&]+)"));
            const auto match = re.match(link);
            if (match.hasMatch()) {
                code = QUrl::fromPercentEncoding(match.captured(1).toUtf8());
            }
        }
    }
    return code;
}

bool AuthSession::completeEmailLink(const QUrl &url, QString *errorCode)
{
    qInfo() << "AuthSession: completeEmailLink" << url.toString(QUrl::RemoveQuery);
    if (hasSession()) {
        qInfo() << "AuthSession: email link ignored, already signed in";
        LocalStore::clearPendingSignInEmail();
        return true;
    }
    const QString oob = oobCodeFromUrl(url);
    const QString email = LocalStore::pendingSignInEmail();
    if (oob.isEmpty() || email.isEmpty()) {
        qWarning() << "AuthSession: email link missing oob or pending email oobEmpty=" << oob.isEmpty()
                   << " emailEmpty=" << email.isEmpty();
        if (errorCode) {
            *errorCode = QStringLiteral("AUTH_LINK_INVALID");
        }
        return false;
    }
    if (!beginAuth(errorCode)) {
        return false;
    }
    FirebaseTokens tokens;
    const bool ok = m_firebase.signInEmailLink(email, oob, &tokens, errorCode);
    if (!ok) {
        endAuth();
        return false;
    }
    const bool saved = finishTokens(tokens, QStringLiteral("link"), errorCode);
    endAuth();
    return saved;
}

// ─── Ariadne's Thread [AT-0090] ─────────────────────
// What: Google via Firebase createAuthUri handler then signInWithIdp
// Why:  Official Identity Toolkit; /__/auth/handler is already on authorized authDomain
// Date: 2026-08-25
// Related: [AT-0098] MacGoogleAuth.h, [AT-0089] FirebaseAuthClient.cpp:createAuthUri
// ─────────────────────────────────────────────────────
bool AuthSession::signInGoogle(QString *errorCode)
{
    qInfo() << "AuthSession: signInGoogle";
    if (!beginAuth(errorCode)) {
        return false;
    }
    const QString continueUri = Config::firebaseAuthHandlerUrl();
    QString authUri;
    QString sessionId;
    if (!m_firebase.createAuthUri(continueUri, &authUri, &sessionId, errorCode)) {
        endAuth();
        return false;
    }
    QString requestUri;
    if (!MacGoogleAuth::captureHandlerRedirect(QUrl(authUri), &requestUri, errorCode)) {
        endAuth();
        return false;
    }
    FirebaseTokens tokens;
    if (!m_firebase.signInWithIdpRedirect(requestUri, sessionId, &tokens, errorCode)) {
        endAuth();
        return false;
    }
    const bool saved = finishTokens(tokens, QStringLiteral("google"), errorCode);
    endAuth();
    return saved;
}

void AuthSession::signOut()
{
    qInfo() << "AuthSession: signOut";
    {
        QMutexLocker lock(&m_mutex);
        m_signedOut = true;
        QString error;
        m_store->remove(QString::fromLatin1(kRefreshKey), &error);
        m_store->remove(QString::fromLatin1(kUidKey), &error);
        m_store->remove(QString::fromLatin1(kEmailKey), &error);
        m_tokens = FirebaseTokens{};
    }
    LocalStore::clearPendingSignInEmail();
    emit sessionChanged();
}

// ─── Ariadne's Thread [AT-0019] ─────────────────────
// What: Serialize ID token refresh on one mutex / one in-flight flag
// Why:  Two parallel API calls must not race refresh
// Date: 2026-08-25
// Related: [AT-0018] AuthSession.h
// ─────────────────────────────────────────────────────
bool AuthSession::ensureIdToken(QString *idToken, QString *errorCode)
{
    QMutexLocker lock(&m_mutex);
    if (m_tokens.refreshToken.isEmpty()) {
        if (errorCode) {
            *errorCode = QStringLiteral("STORAGE_NEED_SIGN_IN");
        }
        return false;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!m_tokens.idToken.isEmpty() && now < m_tokens.expiresAtMs) {
        *idToken = m_tokens.idToken;
        return true;
    }
    if (!isOnline()) {
        if (errorCode) {
            *errorCode = QStringLiteral("OFFLINE_CLOUD_UNAVAILABLE");
        }
        qWarning() << "AuthSession: cannot refresh while offline";
        return false;
    }
    while (m_refreshing) {
        qInfo() << "AuthSession: waiting for in-flight refresh";
        lock.unlock();
        QThread::msleep(50);
        lock.relock();
        if (!m_tokens.idToken.isEmpty() && QDateTime::currentMSecsSinceEpoch() < m_tokens.expiresAtMs) {
            *idToken = m_tokens.idToken;
            return true;
        }
    }
    m_refreshing = true;
    const QString refresh = m_tokens.refreshToken;
    lock.unlock();
    FirebaseTokens tokens;
    QString localError;
    const bool ok = m_firebase.refresh(refresh, &tokens, &localError);
    lock.relock();
    m_refreshing = false;
    if (m_signedOut) {
        qWarning() << "AuthSession: refresh finished after signOut, drop tokens";
        return false;
    }
    if (!ok) {
        if (errorCode) {
            *errorCode = localError.isEmpty() ? QStringLiteral("AUTH_REFRESH_FAILED") : localError;
        }
        return false;
    }
    tokens.email = m_tokens.email;
    m_tokens = tokens;
    persist(tokens, errorCode);
    *idToken = tokens.idToken;
    qInfo() << "AuthSession: ensureIdToken ok uid=" << tokens.uid;
    return true;
}
