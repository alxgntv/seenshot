#include "auth/MacOAuthClient.h"

#include <QDebug>
#include <QMetaObject>
#include <QString>
#include <QUrl>

#import <AppKit/AppKit.h>
#import <CommonCrypto/CommonDigest.h>
#import <Security/Security.h>

namespace {

QByteArray base64Url(const QByteArray &raw)
{
    return raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QByteArray randomBytes(int count, QString *errorCode)
{
    QByteArray out(count, 0);
    const int status = SecRandomCopyBytes(kSecRandomDefault, static_cast<size_t>(count), out.data());
    if (status != errSecSuccess) {
        qWarning() << "MacOAuthClient: SecRandomCopyBytes failed status=" << status;
        if (errorCode) {
            *errorCode = QStringLiteral("AUTH_OAUTH_FAILED");
        }
        return {};
    }
    return out;
}

} // namespace

MacOAuthClient::MacOAuthClient(QObject *parent)
    : QObject(parent)
{
    qInfo() << "MacOAuthClient: constructed";
}

MacOAuthClient::~MacOAuthClient()
{
    ++m_openGeneration;
    qInfo() << "MacOAuthClient: destroyed generation=" << m_openGeneration;
}

bool MacOAuthClient::generatePkce(QString *verifier, QString *challenge, QString *state, QString *errorCode)
{
    const QByteArray verifierRaw = randomBytes(32, errorCode);
    if (verifierRaw.isEmpty()) {
        return false;
    }
    const QByteArray stateRaw = randomBytes(16, errorCode);
    if (stateRaw.isEmpty()) {
        return false;
    }
    const QByteArray verifierB64 = base64Url(verifierRaw);
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(reinterpret_cast<const unsigned char *>(verifierB64.constData()),
              static_cast<CC_LONG>(verifierB64.size()), digest);
    const QByteArray challengeB64 = base64Url(QByteArray(reinterpret_cast<const char *>(digest), CC_SHA256_DIGEST_LENGTH));
    *verifier = QString::fromLatin1(verifierB64);
    *challenge = QString::fromLatin1(challengeB64);
    *state = QString::fromLatin1(base64Url(stateRaw));
    qInfo() << "MacOAuthClient: PKCE verifierChars=" << verifier->size() << " challengeChars=" << challenge->size()
            << " stateChars=" << state->size();
    return true;
}

bool MacOAuthClient::start(const QUrl &authorizeUrl)
{
    if (authorizeUrl.isEmpty()) {
        qWarning() << "MacOAuthClient: empty authorize URL";
        return false;
    }
    NSURL *url = authorizeUrl.toNSURL();
    if (!url) {
        qWarning() << "MacOAuthClient: NSURL conversion failed";
        return false;
    }
    ++m_openGeneration;
    const int generation = m_openGeneration;
    qInfo() << "MacOAuthClient: open default browser generation=" << generation << " host=" << authorizeUrl.host()
            << " path=" << authorizeUrl.path();
    NSWorkspaceOpenConfiguration *config = [NSWorkspaceOpenConfiguration configuration];
    config.activates = YES;
    MacOAuthClient *client = this;
    [[NSWorkspace sharedWorkspace] openURL:url
                             configuration:config
                         completionHandler:^(NSRunningApplication *app, NSError *error) {
                             const bool ok = (error == nil);
                             const QString bundle = app.bundleIdentifier
                                 ? QString::fromNSString(app.bundleIdentifier)
                                 : QString();
                             const QString errText = error
                                 ? QString::fromNSString(error.localizedDescription)
                                 : QString();
                             qInfo() << "MacOAuthClient: NSWorkspace completion generation=" << generation
                                     << " ok=" << ok << " bundle=" << bundle;
                             QMetaObject::invokeMethod(client, "onWorkspaceOpenCompleted", Qt::QueuedConnection,
                                                       Q_ARG(int, generation), Q_ARG(bool, ok), Q_ARG(QString, bundle),
                                                       Q_ARG(QString, errText));
                         }];
    return true;
}

void MacOAuthClient::onWorkspaceOpenCompleted(int generation, bool ok, const QString &bundleId, const QString &errorText)
{
    if (generation != m_openGeneration) {
        qInfo() << "MacOAuthClient: ignore stale open result generation=" << generation
                << " current=" << m_openGeneration;
        return;
    }
    if (!ok) {
        qWarning() << "MacOAuthClient: NSWorkspace open failed" << errorText;
        finishFromBrowser(QUrl(), QStringLiteral("AUTH_OAUTH_FAILED"));
        return;
    }
    qInfo() << "MacOAuthClient: default browser opened bundle=" << bundleId;
}

void MacOAuthClient::finishFromBrowser(const QUrl &callbackUrl, const QString &errorCode)
{
    qInfo() << "MacOAuthClient: finishFromBrowser error=" << errorCode << " hasCallback=" << !callbackUrl.isEmpty();
    emit finished(callbackUrl, errorCode);
}
