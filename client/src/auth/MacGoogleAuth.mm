#include "auth/MacGoogleAuth.h"

#include "app/Config.h"

#include <QDebug>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <memory>

#import <AppKit/AppKit.h>
#import <AuthenticationServices/AuthenticationServices.h>
#import <Foundation/Foundation.h>

@interface SeenShotAuthPresenter : NSObject <ASWebAuthenticationPresentationContextProviding>
@end

@implementation SeenShotAuthPresenter
- (ASPresentationAnchor)presentationAnchorForWebAuthenticationSession:(ASWebAuthenticationSession *)session
{
    (void)session;
    NSWindow *window = NSApp.keyWindow;
    if (!window && NSApp.windows.count > 0) {
        window = NSApp.windows.firstObject;
    }
    qInfo() << "MacGoogleAuth: presentation window null=" << (window == nil)
            << " windowCount=" << (int)NSApp.windows.count;
    return window;
}
@end

namespace {

void logAuthUriQueryKeys(const QUrl &authUri)
{
    const QUrlQuery query(authUri);
    QStringList keys;
    const auto items = query.queryItems();
    for (const auto &item : items) {
        keys.append(item.first);
    }
    qInfo() << "MacGoogleAuth: authUri host=" << authUri.host() << " queryKeys=" << keys
            << " hasPrompt=" << query.hasQueryItem(QStringLiteral("prompt"))
            << " prompt=" << query.queryItemValue(QStringLiteral("prompt"));
}

} // namespace

// ─── Ariadne's Thread [AT-0099] ─────────────────────
// What: Open createAuthUri in ASWebAuthenticationSession; capture /__/auth/handler
// Why:  Official AuthenticationServices shares Safari cookies when ephemeral is off
// Date: 2026-08-25
// Related: [AT-0090] AuthSession.cpp:signInGoogle, [AT-0097] Config.cpp:firebaseAuthHandlerUrl
// ─────────────────────────────────────────────────────
bool MacGoogleAuth::captureHandlerRedirect(const QUrl &authUri, QString *requestUri, QString *errorCode)
{
    if (!authUri.isValid()) {
        qWarning() << "MacGoogleAuth: invalid authUri";
        if (errorCode) {
            *errorCode = QStringLiteral("GOOGLE_SIGN_IN_UNAVAILABLE");
        }
        return false;
    }
    if (@available(macOS 14.4, *)) {
        logAuthUriQueryKeys(authUri);
        const QString handlerHost = Config::firebaseAuthDomain();
        const QString handlerPath = QStringLiteral("/__/auth/handler");
        qInfo() << "MacGoogleAuth: ASWebAuthenticationSession ephemeral=NO host=" << handlerHost
                << " path=" << handlerPath;

        struct WaitState {
            QString captured;
            QString sessionError;
            QEventLoop *loop = nullptr;
        };
        const auto state = std::make_shared<WaitState>();
        QEventLoop loop;
        state->loop = &loop;

        SeenShotAuthPresenter *presenter = [[SeenShotAuthPresenter alloc] init];
        ASWebAuthenticationSessionCallback *callback =
            [ASWebAuthenticationSessionCallback callbackWithHTTPSHost:handlerHost.toNSString()
                                                                 path:handlerPath.toNSString()];
        ASWebAuthenticationSession *session =
            [[ASWebAuthenticationSession alloc] initWithURL:authUri.toNSURL()
                                                   callback:callback
                                          completionHandler:^(NSURL *callbackURL, NSError *error) {
                                              if (error) {
                                                  const bool cancelled =
                                                      error.code == ASWebAuthenticationSessionErrorCodeCanceledLogin;
                                                  qInfo() << "MacGoogleAuth: session error cancelled=" << cancelled
                                                          << " code=" << (int)error.code
                                                          << QString::fromNSString(error.localizedDescription);
                                                  if (state->captured.isEmpty()) {
                                                      state->sessionError =
                                                          cancelled ? QStringLiteral("GOOGLE_SIGN_IN_CANCELLED")
                                                                    : QStringLiteral("GOOGLE_SIGN_IN_UNAVAILABLE");
                                                  }
                                              } else if (callbackURL) {
                                                  const QString href = QString::fromNSString(callbackURL.absoluteString);
                                                  const bool hasToken = href.contains(QLatin1String("id_token="))
                                                      || href.contains(QLatin1String("access_token="));
                                                  qInfo() << "MacGoogleAuth: callback host="
                                                          << QString::fromNSString(callbackURL.host ?: @"")
                                                          << " path="
                                                          << QString::fromNSString(callbackURL.path ?: @"")
                                                          << " hasFragment=" << (callbackURL.fragment.length > 0)
                                                          << " hasToken=" << hasToken
                                                          << " chars=" << href.size();
                                                  state->captured = href;
                                              } else {
                                                  qWarning() << "MacGoogleAuth: session finished with empty callback";
                                                  if (state->captured.isEmpty()) {
                                                      state->sessionError = QStringLiteral("AUTH_REFRESH_FAILED");
                                                  }
                                              }
                                              if (state->loop) {
                                                  state->loop->quit();
                                              }
                                          }];
        session.prefersEphemeralWebBrowserSession = NO;
        session.presentationContextProvider = presenter;
        qInfo() << "MacGoogleAuth: canStart=" << session.canStart;
        if (![session start]) {
            qWarning() << "MacGoogleAuth: ASWebAuthenticationSession start failed";
            if (errorCode) {
                *errorCode = QStringLiteral("GOOGLE_SIGN_IN_UNAVAILABLE");
            }
            return false;
        }

        QTimer::singleShot(180000, &loop, &QEventLoop::quit);
        loop.exec();
        state->loop = nullptr;
        [session cancel];

        if (!state->captured.isEmpty()) {
            *requestUri = state->captured;
            qInfo() << "MacGoogleAuth: captured handler requestUriChars=" << requestUri->size();
            return true;
        }
        if (!state->sessionError.isEmpty()) {
            if (errorCode) {
                *errorCode = state->sessionError;
            }
            return false;
        }
        qWarning() << "MacGoogleAuth: handler capture timeout or empty";
        if (errorCode) {
            *errorCode = QStringLiteral("AUTH_REFRESH_FAILED");
        }
        return false;
    }
    qWarning() << "MacGoogleAuth: HTTPS handler callback requires macOS 14.4";
    if (errorCode) {
        *errorCode = QStringLiteral("GOOGLE_SIGN_IN_UNAVAILABLE");
    }
    return false;
}
