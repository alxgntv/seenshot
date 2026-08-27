#include "auth/FirebaseAuthClient.h"

#include "app/Config.h"

#include <QDateTime>
#include <QDebug>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {

QString mapIdentityMessage(QString message)
{
    const int colon = message.indexOf(QLatin1String(" :"));
    if (colon > 0) {
        message = message.left(colon);
    }
    if (message == QLatin1String("EMAIL_EXISTS") || message == QLatin1String("CREDENTIAL_ALREADY_IN_USE")) {
        return QStringLiteral("EMAIL_IN_USE");
    }
    if (message == QLatin1String("EMAIL_NOT_FOUND")) {
        return QStringLiteral("EMAIL_NOT_FOUND");
    }
    if (message == QLatin1String("INVALID_PASSWORD") || message == QLatin1String("INVALID_LOGIN_CREDENTIALS")) {
        return QStringLiteral("WRONG_PASSWORD");
    }
    if (message == QLatin1String("WEAK_PASSWORD")) {
        return QStringLiteral("WEAK_PASSWORD");
    }
    if (message == QLatin1String("INVALID_EMAIL") || message == QLatin1String("MISSING_EMAIL")) {
        return QStringLiteral("INVALID_EMAIL");
    }
    if (message == QLatin1String("INVALID_OOB_CODE") || message == QLatin1String("EXPIRED_OOB_CODE")) {
        return QStringLiteral("AUTH_LINK_INVALID");
    }
    if (message == QLatin1String("ACCOUNT_EXISTS_WITH_DIFFERENT_CREDENTIAL")) {
        return QStringLiteral("AUTH_ACCOUNT_EXISTS");
    }
    if (message == QLatin1String("OPERATION_NOT_ALLOWED")) {
        return QStringLiteral("AUTH_PROVIDER_DISABLED");
    }
    if (message == QLatin1String("INVALID_CONTINUE_URI")) {
        return QStringLiteral("AUTH_LINK_INVALID");
    }
    if (message == QLatin1String("INVALID_CUSTOM_TOKEN") || message == QLatin1String("CREDENTIAL_MISMATCH")) {
        return QStringLiteral("AUTH_OAUTH_FAILED");
    }
    return QStringLiteral("AUTH_REFRESH_FAILED");
}

QString identityErrorCode(const QByteArray &body)
{
    const QJsonObject err = QJsonDocument::fromJson(body).object().value(QStringLiteral("error")).toObject();
    const QString message = err.value(QStringLiteral("message")).toString();
    const QString code = mapIdentityMessage(message);
    qWarning() << "FirebaseAuthClient: identity error message=" << message << " code=" << code;
    return code;
}

QMutex g_blocklistMutex;
QSet<QString> g_blocklist;
bool g_blocklistReady = false;

bool ensureDisposableBlocklist(QNetworkAccessManager *nam, QString *errorCode)
{
    {
        QMutexLocker locker(&g_blocklistMutex);
        if (g_blocklistReady) {
            qInfo() << "FirebaseAuthClient: disposable blocklist cached size=" << g_blocklist.size();
            return true;
        }
    }
    const QUrl url(QStringLiteral(
        "https://raw.githubusercontent.com/disposable-email-domains/disposable-email-domains/refs/heads/main/"
        "disposable_email_blocklist.conf"));
    qInfo() << "FirebaseAuthClient: fetching disposable blocklist";
    QNetworkRequest request(url);
    QNetworkReply *reply = nam->get(request);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(20000);
    loop.exec();
    if (!reply->isFinished()) {
        qWarning() << "FirebaseAuthClient: disposable blocklist timeout";
        reply->abort();
        reply->deleteLater();
        if (errorCode) {
            *errorCode = QStringLiteral("AUTH_REFRESH_FAILED");
        }
        return false;
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const QNetworkReply::NetworkError error = reply->error();
    reply->deleteLater();
    qInfo() << "FirebaseAuthClient: disposable blocklist status=" << status << " bytes=" << body.size()
            << " error=" << error;
    if (status < 200 || status >= 300) {
        if (errorCode) {
            *errorCode = QStringLiteral("AUTH_REFRESH_FAILED");
        }
        return false;
    }
    QSet<QString> parsed;
    const QString text = QString::fromUtf8(body);
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\\r?\\n")));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1String("//"))) {
            continue;
        }
        parsed.insert(line.toLower());
    }
    qInfo() << "FirebaseAuthClient: disposable blocklist parsed size=" << parsed.size();
    if (parsed.isEmpty()) {
        qWarning() << "FirebaseAuthClient: disposable blocklist empty";
        if (errorCode) {
            *errorCode = QStringLiteral("AUTH_REFRESH_FAILED");
        }
        return false;
    }
    QMutexLocker locker(&g_blocklistMutex);
    if (!g_blocklistReady) {
        g_blocklist = parsed;
        g_blocklistReady = true;
    }
    return true;
}

bool isDisposableEmail(const QString &email)
{
    const int at = email.indexOf(QLatin1Char('@'));
    if (at < 0) {
        qWarning() << "FirebaseAuthClient: disposable check missing @";
        return false;
    }
    const QString domain = email.mid(at + 1).trimmed().toLower();
    const QStringList domainParts = domain.split(QLatin1Char('.'));
    QMutexLocker locker(&g_blocklistMutex);
    for (int i = 0; i < domainParts.size() - 1; ++i) {
        QString candidate;
        for (int j = i; j < domainParts.size(); ++j) {
            if (j > i) {
                candidate += QLatin1Char('.');
            }
            candidate += domainParts.at(j);
        }
        if (g_blocklist.contains(candidate)) {
            qWarning() << "FirebaseAuthClient: disposable domain=" << candidate
                       << " domainParts=" << domainParts.size();
            return true;
        }
    }
    qInfo() << "FirebaseAuthClient: permanent email domainParts=" << domainParts.size();
    return false;
}

// ─── Ariadne's Thread [AT-0188] ─────────────────────
// What: Reject throwaway mail before Identity Toolkit sign-in and sign-up
// Why:  Official disposable-email-domains list; same message as the website
// Date: 2026-08-27
// Related: [AT-0186] backend→disposableEmail.ts:isDisposableEmail, seenshot-web→src/disposableEmail.ts
// ─────────────────────────────────────────────────────
bool allowPermanentEmail(QNetworkAccessManager *nam, const QString &email, QString *errorCode)
{
    if (!ensureDisposableBlocklist(nam, errorCode)) {
        qWarning() << "FirebaseAuthClient: disposable blocklist unavailable";
        return false;
    }
    if (isDisposableEmail(email)) {
        if (errorCode) {
            *errorCode = QStringLiteral("AUTH_DISPOSABLE_EMAIL");
        }
        return false;
    }
    return true;
}

} // namespace

FirebaseAuthClient::FirebaseAuthClient(QNetworkAccessManager *nam)
    : m_nam(nam)
{
}

QUrl FirebaseAuthClient::identityUrl(const QString &path) const
{
    QUrl url(QStringLiteral("https://identitytoolkit.googleapis.com/v1/") + path);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), Config::firebaseApiKey());
    url.setQuery(query);
    return url;
}

bool FirebaseAuthClient::postJson(const QUrl &url, const QByteArray &body, QByteArray *response, QString *errorCode)
{
    qInfo() << "FirebaseAuthClient: POST" << url.toString(QUrl::RemoveQuery);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = m_nam->post(request, body);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(20000);
    loop.exec();
    if (!reply->isFinished()) {
        qWarning() << "FirebaseAuthClient: timeout";
        reply->abort();
        reply->deleteLater();
        if (errorCode) {
            *errorCode = QStringLiteral("AUTH_REFRESH_FAILED");
        }
        return false;
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    *response = reply->readAll();
    const QNetworkReply::NetworkError error = reply->error();
    reply->deleteLater();
    qInfo() << "FirebaseAuthClient: status=" << status << " bytes=" << response->size() << " error=" << error;
    if (status < 200 || status >= 300) {
        if (errorCode) {
            *errorCode = identityErrorCode(*response);
        }
        qWarning() << "FirebaseAuthClient: body" << QString::fromUtf8(*response).left(400);
        return false;
    }
    return true;
}

bool FirebaseAuthClient::parseTokens(const QByteArray &response, FirebaseTokens *out, QString *errorCode)
{
    const QJsonObject json = QJsonDocument::fromJson(response).object();
    out->idToken = json.value(QStringLiteral("idToken")).toString();
    out->refreshToken = json.value(QStringLiteral("refreshToken")).toString();
    out->uid = json.value(QStringLiteral("localId")).toString();
    out->email = json.value(QStringLiteral("email")).toString();
    const int expires = json.value(QStringLiteral("expiresIn")).toString().toInt();
    out->expiresAtMs = QDateTime::currentMSecsSinceEpoch() + (expires > 0 ? expires * 1000 : 3600 * 1000) - 60000;
    qInfo() << "FirebaseAuthClient: tokens uid=" << out->uid << " emailChars=" << out->email.size()
            << " expiresAt=" << out->expiresAtMs;
    if (out->idToken.isEmpty() || out->refreshToken.isEmpty()) {
        if (errorCode) {
            *errorCode = QStringLiteral("AUTH_REFRESH_FAILED");
        }
        return false;
    }
    return true;
}

bool FirebaseAuthClient::signInEmail(const QString &email, const QString &password, FirebaseTokens *out,
                                    QString *errorCode)
{
    const QString key = Config::firebaseApiKey();
    if (key.isEmpty()) {
        qWarning() << "FirebaseAuthClient: missing API key";
        if (errorCode) {
            *errorCode = QStringLiteral("AUTH_REFRESH_FAILED");
        }
        return false;
    }
    qInfo() << "FirebaseAuthClient: signInEmail emailChars=" << email.size();
    if (!allowPermanentEmail(m_nam, email, errorCode)) {
        return false;
    }
    QJsonObject body;
    body.insert(QStringLiteral("email"), email);
    body.insert(QStringLiteral("password"), password);
    body.insert(QStringLiteral("returnSecureToken"), true);
    QByteArray response;
    if (!postJson(identityUrl(QStringLiteral("accounts:signInWithPassword")),
                  QJsonDocument(body).toJson(QJsonDocument::Compact), &response, errorCode)) {
        return false;
    }
    return parseTokens(response, out, errorCode);
}

// ─── Ariadne's Thread [AT-0084] ─────────────────────
// What: signUp, reset, email link, lookup on the same REST client
// Why:  PRD-04 forbids a second auth stack next to Identity Toolkit
// Date: 2026-08-25
// Related: [AT-0016] FirebaseAuthClient.h, docs/PRD-04-settings-auth.md
// ─────────────────────────────────────────────────────
bool FirebaseAuthClient::signUpEmail(const QString &email, const QString &password, FirebaseTokens *out,
                                    QString *errorCode)
{
    qInfo() << "FirebaseAuthClient: signUpEmail emailChars=" << email.size();
    if (!allowPermanentEmail(m_nam, email, errorCode)) {
        return false;
    }
    QJsonObject body;
    body.insert(QStringLiteral("email"), email);
    body.insert(QStringLiteral("password"), password);
    body.insert(QStringLiteral("returnSecureToken"), true);
    QByteArray response;
    if (!postJson(identityUrl(QStringLiteral("accounts:signUp")), QJsonDocument(body).toJson(QJsonDocument::Compact),
                  &response, errorCode)) {
        return false;
    }
    return parseTokens(response, out, errorCode);
}

bool FirebaseAuthClient::sendPasswordReset(const QString &email, QString *errorCode)
{
    qInfo() << "FirebaseAuthClient: sendPasswordReset emailChars=" << email.size();
    if (!allowPermanentEmail(m_nam, email, errorCode)) {
        return false;
    }
    QJsonObject body;
    body.insert(QStringLiteral("requestType"), QStringLiteral("PASSWORD_RESET"));
    body.insert(QStringLiteral("email"), email);
    QByteArray response;
    return postJson(identityUrl(QStringLiteral("accounts:sendOobCode")),
                    QJsonDocument(body).toJson(QJsonDocument::Compact), &response, errorCode);
}

bool FirebaseAuthClient::sendEmailLink(const QString &email, QString *errorCode)
{
    qInfo() << "FirebaseAuthClient: sendEmailLink continue=" << Config::emailLinkContinueUrl()
            << " emailChars=" << email.size();
    if (!allowPermanentEmail(m_nam, email, errorCode)) {
        return false;
    }
    QJsonObject body;
    body.insert(QStringLiteral("requestType"), QStringLiteral("EMAIL_SIGNIN"));
    body.insert(QStringLiteral("email"), email);
    body.insert(QStringLiteral("continueUrl"), Config::emailLinkContinueUrl());
    body.insert(QStringLiteral("canHandleCodeInApp"), true);
    QByteArray response;
    return postJson(identityUrl(QStringLiteral("accounts:sendOobCode")),
                    QJsonDocument(body).toJson(QJsonDocument::Compact), &response, errorCode);
}

bool FirebaseAuthClient::signInEmailLink(const QString &email, const QString &oobCode, FirebaseTokens *out,
                                         QString *errorCode)
{
    qInfo() << "FirebaseAuthClient: signInEmailLink emailChars=" << email.size();
    if (!allowPermanentEmail(m_nam, email, errorCode)) {
        return false;
    }
    QJsonObject body;
    body.insert(QStringLiteral("email"), email);
    body.insert(QStringLiteral("oobCode"), oobCode);
    QByteArray response;
    if (!postJson(identityUrl(QStringLiteral("accounts:signInWithEmailLink")),
                  QJsonDocument(body).toJson(QJsonDocument::Compact), &response, errorCode)) {
        return false;
    }
    return parseTokens(response, out, errorCode);
}

// ─── Ariadne's Thread [AT-0192] ─────────────────────
// What: Exchange a Firebase custom token for id and refresh tokens
// Why:  Website OAuth token endpoint mints a custom token for the Mac public client
// Date: 2026-08-27
// Related: [AT-0193] AuthSession.cpp:completeWebsiteCallback, [AT-0040] seenshot-web→src/oauth.ts
// ─────────────────────────────────────────────────────
bool FirebaseAuthClient::signInWithCustomToken(const QString &customToken, FirebaseTokens *out, QString *errorCode)
{
    const QString key = Config::firebaseApiKey();
    if (key.isEmpty()) {
        qWarning() << "FirebaseAuthClient: missing API key";
        if (errorCode) {
            *errorCode = QStringLiteral("AUTH_OAUTH_FAILED");
        }
        return false;
    }
    qInfo() << "FirebaseAuthClient: signInWithCustomToken chars=" << customToken.size();
    QJsonObject body;
    body.insert(QStringLiteral("token"), customToken);
    body.insert(QStringLiteral("returnSecureToken"), true);
    QByteArray response;
    if (!postJson(identityUrl(QStringLiteral("accounts:signInWithCustomToken")),
                  QJsonDocument(body).toJson(QJsonDocument::Compact), &response, errorCode)) {
        return false;
    }
    return parseTokens(response, out, errorCode);
}

bool FirebaseAuthClient::lookupEmail(const QString &idToken, QString *email, QString *errorCode)
{
    QJsonObject body;
    body.insert(QStringLiteral("idToken"), idToken);
    QByteArray response;
    qInfo() << "FirebaseAuthClient: accounts:lookup";
    if (!postJson(identityUrl(QStringLiteral("accounts:lookup")), QJsonDocument(body).toJson(QJsonDocument::Compact),
                  &response, errorCode)) {
        return false;
    }
    const QJsonObject json = QJsonDocument::fromJson(response).object();
    const auto users = json.value(QStringLiteral("users")).toArray();
    if (users.isEmpty()) {
        qWarning() << "FirebaseAuthClient: lookup empty users";
        if (errorCode) {
            *errorCode = QStringLiteral("AUTH_REFRESH_FAILED");
        }
        return false;
    }
    *email = users.at(0).toObject().value(QStringLiteral("email")).toString();
    qInfo() << "FirebaseAuthClient: lookup emailChars=" << email->size();
    return !email->isEmpty();
}

// ─── Ariadne's Thread [AT-0017] ─────────────────────
// What: Refresh ID token via securetoken.googleapis.com
// Why:  Session is permanent until logout
// Date: 2026-08-25
// Related: [AT-0016] FirebaseAuthClient.h
// ─────────────────────────────────────────────────────
bool FirebaseAuthClient::refresh(const QString &refreshToken, FirebaseTokens *out, QString *errorCode)
{
    const QString key = Config::firebaseApiKey();
    QUrl url(QStringLiteral("https://securetoken.googleapis.com/v1/token"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), key);
    url.setQuery(query);
    const QByteArray form = "grant_type=refresh_token&refresh_token=" + QUrl::toPercentEncoding(refreshToken);
    qInfo() << "FirebaseAuthClient: refresh token";
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    QNetworkReply *reply = m_nam->post(request, form);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    loop.exec();
    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        if (errorCode) {
            *errorCode = QStringLiteral("AUTH_REFRESH_FAILED");
        }
        return false;
    }
    const QByteArray response = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();
    if (status < 200 || status >= 300) {
        qWarning() << "FirebaseAuthClient: refresh failed" << status << QString::fromUtf8(response).left(400);
        if (errorCode) {
            *errorCode = identityErrorCode(response);
        }
        return false;
    }
    const QJsonObject json = QJsonDocument::fromJson(response).object();
    out->idToken = json.value(QStringLiteral("id_token")).toString();
    out->refreshToken = json.value(QStringLiteral("refresh_token")).toString();
    out->uid = json.value(QStringLiteral("user_id")).toString();
    const int expires = json.value(QStringLiteral("expires_in")).toString().toInt();
    out->expiresAtMs = QDateTime::currentMSecsSinceEpoch() + (expires > 0 ? expires * 1000 : 3600 * 1000) - 60000;
    qInfo() << "FirebaseAuthClient: refreshed uid=" << out->uid;
    return !out->idToken.isEmpty() && !out->refreshToken.isEmpty();
}
