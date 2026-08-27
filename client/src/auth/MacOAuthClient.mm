#include "auth/MacOAuthClient.h"

#include "app/MacPermissions.h"

#include <QDebug>
#include <QMetaObject>
#include <QString>
#include <QUrl>

#import <AppKit/AppKit.h>
#import <AuthenticationServices/AuthenticationServices.h>
#import <CommonCrypto/CommonDigest.h>
#import <CoreFoundation/CoreFoundation.h>
#import <Security/Security.h>

@interface SeenShotOAuthNative : NSObject <ASWebAuthenticationPresentationContextProviding>
@property (nonatomic, strong) ASWebAuthenticationSession *session;
@property (nonatomic, strong) NSPanel *anchorPanel;
@property (nonatomic, assign) MacOAuthClient *client;
@end

@implementation SeenShotOAuthNative

- (ASPresentationAnchor)presentationAnchorForWebAuthenticationSession:(ASWebAuthenticationSession *)session
{
    (void)session;
    NSWindow *key = [NSApp keyWindow];
    if (key) {
        qInfo() << "MacOAuthClient: presentation anchor keyWindow";
        return key;
    }
    NSWindow *main = [NSApp mainWindow];
    if (main) {
        qInfo() << "MacOAuthClient: presentation anchor mainWindow";
        return main;
    }
    if (!self.anchorPanel) {
        self.anchorPanel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 1, 1)
                                                     styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                                                       backing:NSBackingStoreBuffered
                                                         defer:NO];
        self.anchorPanel.releasedWhenClosed = NO;
        [self.anchorPanel setFrameOrigin:NSMakePoint(-10000, -10000)];
        [self.anchorPanel orderFrontRegardless];
        qInfo() << "MacOAuthClient: presentation anchor created panel";
    }
    return self.anchorPanel;
}

@end

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

SeenShotOAuthNative *nativeFrom(void *ptr)
{
    return (__bridge SeenShotOAuthNative *)ptr;
}

} // namespace

MacOAuthClient::MacOAuthClient(QObject *parent)
    : QObject(parent)
{
    qInfo() << "MacOAuthClient: constructed";
}

MacOAuthClient::~MacOAuthClient()
{
    if (!m_native) {
        return;
    }
    SeenShotOAuthNative *native = nativeFrom(m_native);
    if (native.session) {
        [native.session cancel];
        qInfo() << "MacOAuthClient: cancelled session in destructor";
    }
    [native.anchorPanel close];
    native.session = nil;
    native.anchorPanel = nil;
    native.client = nullptr;
    CFRelease(m_native);
    m_native = nullptr;
    qInfo() << "MacOAuthClient: destroyed";
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
    if (!m_native) {
        SeenShotOAuthNative *created = [[SeenShotOAuthNative alloc] init];
        created.client = this;
        m_native = (void *)CFBridgingRetain(created);
    }
    SeenShotOAuthNative *native = nativeFrom(m_native);
    if (native.session) {
        qWarning() << "MacOAuthClient: session already running";
        return false;
    }
    NSURL *url = authorizeUrl.toNSURL();
    if (!url) {
        qWarning() << "MacOAuthClient: NSURL conversion failed";
        return false;
    }
    MacPermissions::activateApp();
    qInfo() << "MacOAuthClient: start host=" << authorizeUrl.host() << " path=" << authorizeUrl.path();
    MacOAuthClient *client = this;
    ASWebAuthenticationSession *session = [[ASWebAuthenticationSession alloc]
        initWithURL:url
        callbackURLScheme:@"seenshot"
        completionHandler:^(NSURL *callbackURL, NSError *error) {
            QUrl callback;
            QString code;
            if (error) {
                const BOOL canceled =
                    [error.domain isEqualToString:ASWebAuthenticationSessionErrorDomain] &&
                    error.code == ASWebAuthenticationSessionErrorCodeCanceledLogin;
                code = canceled ? QStringLiteral("AUTH_OAUTH_DENIED") : QStringLiteral("AUTH_OAUTH_FAILED");
                qWarning() << "MacOAuthClient: session error canceled=" << canceled
                           << " code=" << static_cast<int>(error.code);
            } else if (callbackURL) {
                callback = QUrl(QString::fromNSString(callbackURL.absoluteString));
                qInfo() << "MacOAuthClient: session callback host=" << callback.host()
                        << " hasQuery=" << !callback.query().isEmpty();
            } else {
                code = QStringLiteral("AUTH_OAUTH_FAILED");
                qWarning() << "MacOAuthClient: session finished with no URL";
            }
            QMetaObject::invokeMethod(client, "finishFromBrowser", Qt::QueuedConnection, Q_ARG(QUrl, callback),
                                      Q_ARG(QString, code));
        }];
    session.prefersEphemeralWebBrowserSession = NO;
    session.presentationContextProvider = native;
    native.session = session;
    const BOOL started = [session start];
    qInfo() << "MacOAuthClient: start returned=" << static_cast<bool>(started);
    if (!started) {
        native.session = nil;
        return false;
    }
    return true;
}

void MacOAuthClient::finishFromBrowser(const QUrl &callbackUrl, const QString &errorCode)
{
    SeenShotOAuthNative *native = nativeFrom(m_native);
    if (native) {
        native.session = nil;
        [native.anchorPanel close];
        native.anchorPanel = nil;
    }
    qInfo() << "MacOAuthClient: finishFromBrowser error=" << errorCode << " hasCallback=" << !callbackUrl.isEmpty();
    emit finished(callbackUrl, errorCode);
}
