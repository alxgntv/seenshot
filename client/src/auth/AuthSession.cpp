#include "auth/AuthSession.h"

#include "app/Analytics.h"

#include "app/Config.h"
#include "auth/ISecureStore.h"
#include "auth/MacOAuthClient.h"
#include "local/LocalStore.h"

#include <QDateTime>
#include <QDebug>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkInformation>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <QUrlQuery>

namespace {
constexpr auto kRefreshKey = "refresh_token";
constexpr auto kUidKey = "uid";
constexpr auto kEmailKey = "email";
constexpr auto kOauthVerifierKey = "oauth_verifier";
constexpr auto kOauthStateKey = "oauth_state";
constexpr auto kOauthCreatedKey = "oauth_created_ms";
constexpr qint64 kOauthPendingTtlMs = 10 * 60 * 1000;
} // namespace

AuthSession::AuthSession(ISecureStore *store, QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_nam(nam)
    , m_firebase(nam)
    , m_oauth(new MacOAuthClient(this))
{
    connect(m_oauth, &MacOAuthClient::finished, this, &AuthSession::onOAuthBrowserFinished);
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
        m_store->remove(QString::fromLatin1(kOauthVerifierKey), &error);
        m_store->remove(QString::fromLatin1(kOauthStateKey), &error);
        m_store->remove(QString::fromLatin1(kOauthCreatedKey), &error);
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

bool AuthSession::persistPendingPkce(const QString &verifier, const QString &state, QString *errorCode)
{
    const QByteArray created = QByteArray::number(QDateTime::currentMSecsSinceEpoch());
    if (!m_store->write(QString::fromLatin1(kOauthVerifierKey), verifier.toUtf8(), errorCode)) {
        qWarning() << "AuthSession: persist oauth verifier failed";
        return false;
    }
    if (!m_store->write(QString::fromLatin1(kOauthStateKey), state.toUtf8(), errorCode)) {
        qWarning() << "AuthSession: persist oauth state failed";
        return false;
    }
    if (!m_store->write(QString::fromLatin1(kOauthCreatedKey), created, errorCode)) {
        qWarning() << "AuthSession: persist oauth created failed";
        return false;
    }
    qInfo() << "AuthSession: pending PKCE stored stateChars=" << state.size();
    return true;
}

void AuthSession::clearPendingPkce()
{
    QString error;
    m_store->remove(QString::fromLatin1(kOauthVerifierKey), &error);
    m_store->remove(QString::fromLatin1(kOauthStateKey), &error);
    m_store->remove(QString::fromLatin1(kOauthCreatedKey), &error);
    qInfo() << "AuthSession: pending PKCE cleared";
}

bool AuthSession::takePendingPkce(QString *verifier, QString *state, QString *errorCode)
{
    QString error;
    const QByteArray rawVerifier = m_store->read(QString::fromLatin1(kOauthVerifierKey), &error);
    const QByteArray rawState = m_store->read(QString::fromLatin1(kOauthStateKey), &error);
    const QByteArray rawCreated = m_store->read(QString::fromLatin1(kOauthCreatedKey), &error);
    clearPendingPkce();
    if (rawVerifier.isEmpty() || rawState.isEmpty()) {
        qInfo() << "AuthSession: takePendingPkce empty";
        if (errorCode) {
            *errorCode = QString();
        }
        return false;
    }
    const qint64 created = rawCreated.toLongLong();
    const qint64 age = QDateTime::currentMSecsSinceEpoch() - created;
    if (created <= 0 || age > kOauthPendingTtlMs) {
        qWarning() << "AuthSession: pending PKCE expired ageMs=" << age;
        if (errorCode) {
            *errorCode = QStringLiteral("AUTH_OAUTH_FAILED");
        }
        return false;
    }
    *verifier = QString::fromUtf8(rawVerifier);
    *state = QString::fromUtf8(rawState);
    qInfo() << "AuthSession: takePendingPkce ok ageMs=" << age << " stateChars=" << state->size();
    return true;
}

bool AuthSession::exchangeAuthorizationCode(const QString &code, const QString &verifier, QString *customToken,
                                            QString *errorCode)
{
    const QUrl tokenUrl(Config::websiteBaseUrl() + QStringLiteral("/oauth/token"));
    qInfo() << "AuthSession: POST oauth token host=" << tokenUrl.host();
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    form.addQueryItem(QStringLiteral("code"), code);
    form.addQueryItem(QStringLiteral("redirect_uri"), Config::oauthRedirectUri());
    form.addQueryItem(QStringLiteral("client_id"), Config::oauthClientId());
    form.addQueryItem(QStringLiteral("code_verifier"), verifier);
    QNetworkRequest request(tokenUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    QNetworkReply *reply = m_nam->post(request, form.query(QUrl::FullyEncoded).toUtf8());
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(20000);
    loop.exec();
    if (!reply->isFinished()) {
        qWarning() << "AuthSession: oauth token timeout";
        reply->abort();
        reply->deleteLater();
        if (errorCode) {
            *errorCode = QStringLiteral("AUTH_OAUTH_FAILED");
        }
        return false;
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();
    qInfo() << "AuthSession: oauth token status=" << status << " bytes=" << body.size();
    const QJsonObject json = QJsonDocument::fromJson(body).object();
    if (status < 200 || status >= 300) {
        const QString oauthErr = json.value(QStringLiteral("error")).toString();
        qWarning() << "AuthSession: oauth token error=" << oauthErr;
        if (errorCode) {
            *errorCode = QStringLiteral("AUTH_OAUTH_FAILED");
        }
        return false;
    }
    const QString token = json.value(QStringLiteral("custom_token")).toString();
    if (token.isEmpty()) {
        qWarning() << "AuthSession: oauth token missing custom_token";
        if (errorCode) {
            *errorCode = QStringLiteral("AUTH_OAUTH_FAILED");
        }
        return false;
    }
    *customToken = token;
    return true;
}

// ─── Ariadne's Thread [AT-0193] ─────────────────────
// What: Start seenshot.app Authorization Code + PKCE and finish AuthSession
// Why:  Mac public client; Firebase session persists until Sign Out
// Date: 2026-08-27
// Related: [AT-0191] MacOAuthClient.mm, [AT-0192] FirebaseAuthClient.cpp:signInWithCustomToken
// ─────────────────────────────────────────────────────
bool AuthSession::startWebsiteSignIn(QString *errorCode)
{
    qInfo() << "AuthSession: startWebsiteSignIn hasSession=" << hasSession();
    if (hasSession()) {
        qInfo() << "AuthSession: startWebsiteSignIn skipped, already signed in";
        return true;
    }
    if (!beginAuth(errorCode)) {
        return false;
    }
    QString verifier;
    QString challenge;
    QString state;
    if (!MacOAuthClient::generatePkce(&verifier, &challenge, &state, errorCode)) {
        endAuth();
        return false;
    }
    if (!persistPendingPkce(verifier, state, errorCode)) {
        endAuth();
        return false;
    }
    QUrl url(Config::websiteBaseUrl() + QStringLiteral("/oauth/authorize"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("client_id"), Config::oauthClientId());
    query.addQueryItem(QStringLiteral("redirect_uri"), Config::oauthRedirectUri());
    query.addQueryItem(QStringLiteral("code_challenge"), challenge);
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    query.addQueryItem(QStringLiteral("state"), state);
    url.setQuery(query);
    if (!m_oauth->start(url)) {
        qWarning() << "AuthSession: ASWebAuthenticationSession failed to start";
        clearPendingPkce();
        endAuth();
        if (errorCode) {
            *errorCode = QStringLiteral("AUTH_OAUTH_FAILED");
        }
        return false;
    }
    qInfo() << "AuthSession: website sign-in browser started";
    return true;
}

void AuthSession::onOAuthBrowserFinished(const QUrl &callbackUrl, const QString &errorCode)
{
    qInfo() << "AuthSession: onOAuthBrowserFinished error=" << errorCode
            << " hasCallback=" << !callbackUrl.isEmpty();
    if (!errorCode.isEmpty() && callbackUrl.isEmpty()) {
        clearPendingPkce();
        endAuth();
        emit websiteSignInSettled(errorCode);
        return;
    }
    QString localError;
    if (!completeWebsiteCallback(callbackUrl, &localError)) {
        qWarning() << "AuthSession: website callback failed code=" << localError;
    }
}

bool AuthSession::completeWebsiteCallback(const QUrl &url, QString *errorCode)
{
    qInfo() << "AuthSession: completeWebsiteCallback scheme=" << url.scheme() << " host=" << url.host();
    const QUrlQuery query(url);
    const QString oauthError = query.queryItemValue(QStringLiteral("error"));
    if (oauthError == QLatin1String("access_denied")) {
        qInfo() << "AuthSession: oauth access_denied";
        clearPendingPkce();
        endAuth();
        if (errorCode) {
            *errorCode = QStringLiteral("AUTH_OAUTH_DENIED");
        }
        emit websiteSignInSettled(QStringLiteral("AUTH_OAUTH_DENIED"));
        return false;
    }
    QString verifier;
    QString state;
    QString takeError;
    if (!takePendingPkce(&verifier, &state, &takeError)) {
        if (takeError.isEmpty()) {
            qInfo() << "AuthSession: oauth callback ignored, no pending PKCE";
            if (errorCode) {
                *errorCode = QString();
            }
            return true;
        }
        qWarning() << "AuthSession: pending PKCE unusable code=" << takeError;
        endAuth();
        if (errorCode) {
            *errorCode = takeError;
        }
        emit websiteSignInSettled(takeError);
        return false;
    }
    const QString returnedState = query.queryItemValue(QStringLiteral("state"));
    if (returnedState != state) {
        qWarning() << "AuthSession: oauth state mismatch returnedChars=" << returnedState.size()
                   << " expectedChars=" << state.size();
        endAuth();
        if (errorCode) {
            *errorCode = QStringLiteral("AUTH_OAUTH_STATE");
        }
        emit websiteSignInSettled(QStringLiteral("AUTH_OAUTH_STATE"));
        return false;
    }
    const QString code = query.queryItemValue(QStringLiteral("code"));
    if (code.isEmpty()) {
        qWarning() << "AuthSession: oauth callback missing code";
        endAuth();
        if (errorCode) {
            *errorCode = QStringLiteral("AUTH_OAUTH_FAILED");
        }
        emit websiteSignInSettled(QStringLiteral("AUTH_OAUTH_FAILED"));
        return false;
    }
    QString customToken;
    if (!exchangeAuthorizationCode(code, verifier, &customToken, errorCode)) {
        endAuth();
        const QString settled = errorCode && !errorCode->isEmpty() ? *errorCode : QStringLiteral("AUTH_OAUTH_FAILED");
        emit websiteSignInSettled(settled);
        return false;
    }
    FirebaseTokens tokens;
    if (!m_firebase.signInWithCustomToken(customToken, &tokens, errorCode)) {
        endAuth();
        const QString settled = errorCode && !errorCode->isEmpty() ? *errorCode : QStringLiteral("AUTH_OAUTH_FAILED");
        emit websiteSignInSettled(settled);
        return false;
    }
    const bool saved = finishTokens(tokens, QStringLiteral("oauth"), errorCode);
    endAuth();
    if (!saved) {
        const QString settled = errorCode && !errorCode->isEmpty() ? *errorCode : QStringLiteral("AUTH_OAUTH_FAILED");
        emit websiteSignInSettled(settled);
        return false;
    }
    qInfo() << "AuthSession: website sign-in finished uid=" << tokens.uid;
    emit websiteSignInSettled(QString());
    return true;
}
