#include "errors/ErrorCatalog.h"

#include <QDebug>
#include <QHash>

// ─── Ariadne's Thread [AT-0002] ─────────────────────
// What: Map error codes to English UI strings
// Why:  One catalog for dialogs and API payloads
// Date: 2026-08-25
// Related: [AT-0001] ErrorCatalog.h
// ─────────────────────────────────────────────────────
// ─── Ariadne's Thread [AT-0171] ─────────────────────
// What: Drop Keychain wording from KEYCHAIN_UNAVAILABLE
// Why:  Session is QSettings; UI must not mention Keychain
// Date: 2026-08-26
// Related: [AT-0171] KeychainStore.cpp
// ─────────────────────────────────────────────────────
// ─── Ariadne's Thread [AT-0182] ─────────────────────
// What: Explain Screen Recording switch is per signed copy
// Why:  Open Settings with the switch already on left users stuck
// Date: 2026-08-26
// Related: [AT-0176] Application.cpp:showScreenRecordingHelp
// ─────────────────────────────────────────────────────
QString ErrorCatalog::message(const QString &code)
{
    static const QHash<QString, QString> kMessages = {
        {QStringLiteral("OFFLINE_CLOUD_UNAVAILABLE"),
         QStringLiteral("You are offline. Use Save to keep a local PNG. Cloud save and sharing need an internet connection.")},
        {QStringLiteral("SCREEN_RECORDING_DENIED"),
         QStringLiteral("SeenShot cannot capture this screen yet. Open System Settings → Privacy & Security → Screen Recording. If the SeenShot switch is already on, turn it off, then on again. macOS stores that switch per signed copy of the app. Then restart SeenShot.")},
        {QStringLiteral("SCREEN_CAPTURE_BLOCKED"),
         QStringLiteral("This screen could not be captured. The app may be blocking screenshots.")},
        {QStringLiteral("AUTH_REFRESH_FAILED"),
         QStringLiteral("Could not refresh your sign-in. Check your internet connection and try again.")},
        {QStringLiteral("CLOUD_IMAGE_REJECTED"),
         QStringLiteral("The screenshot was rejected by the server. Try capturing again.")},
        {QStringLiteral("PUBLISH_FAILED"),
         QStringLiteral("Could not publish this screenshot. Try again in a moment.")},
        {QStringLiteral("QUOTA_EVICTED"),
         QStringLiteral("Oldest cloud screenshots were removed to free 10 MB.")},
        {QStringLiteral("PRO_GRACE_ENDED"),
         QStringLiteral("Pro ended. Cloud screenshots were removed. Local files are kept.")},
        {QStringLiteral("ACCOUNT_DELETED"),
         QStringLiteral("Your SeenShot account and cloud data were deleted.")},
        {QStringLiteral("HOTKEY_IN_USE"),
         QStringLiteral("That shortcut is already used. Choose a different one.")},
        {QStringLiteral("KEYCHAIN_UNAVAILABLE"),
         QStringLiteral("Could not save your sign-in. Try signing in again.")},
        {QStringLiteral("INBOX_EXPIRED"),
         QStringLiteral("The upload expired before it was confirmed. Upload the screenshot again.")},
        {QStringLiteral("STORAGE_NEED_SIGN_IN"),
         QStringLiteral("Sign in to save to the cloud or share a link.")},
        {QStringLiteral("LOCAL_SAVE_FAILED"),
         QStringLiteral("Could not save the PNG to disk.")},
        {QStringLiteral("UPLOAD_FAILED"),
         QStringLiteral("Could not upload the screenshot. Check your connection and try again.")},
        {QStringLiteral("EXPORT_FAILED"),
         QStringLiteral("Could not export your data.")},
        {QStringLiteral("DELETE_ACCOUNT_FAILED"),
         QStringLiteral("Could not delete your account. Try again.")},
        {QStringLiteral("CAMERA_DENIED"),
         QStringLiteral("Turn on Camera for SeenShot in System Settings, then try Photo again.")},
        {QStringLiteral("CAMERA_UNAVAILABLE"),
         QStringLiteral("No camera is available.")},
        {QStringLiteral("PHOTO_CAPTURE_FAILED"),
         QStringLiteral("Could not take the photo. Try again.")},
        {QStringLiteral("PHOTO_NO_PERSON"),
         QStringLiteral("No person was found. Stand in the frame and try again.")},
        {QStringLiteral("PHOTO_CUTOUT_FAILED"),
         QStringLiteral("Could not remove the background. Try again.")},
        {QStringLiteral("AUTH_EMAIL_REQUIRED"), QStringLiteral("Enter your email address.")},
        {QStringLiteral("AUTH_PASSWORD_REQUIRED"), QStringLiteral("Enter your password.")},
        {QStringLiteral("INVALID_EMAIL"), QStringLiteral("That email address is not valid.")},
        {QStringLiteral("EMAIL_IN_USE"),
         QStringLiteral("An account with this email already exists. Sign in instead.")},
        {QStringLiteral("EMAIL_NOT_FOUND"),
         QStringLiteral("No account uses this email. Create an account or use another sign-in method.")},
        {QStringLiteral("WRONG_PASSWORD"), QStringLiteral("Incorrect password. Try again or reset it.")},
        {QStringLiteral("WEAK_PASSWORD"), QStringLiteral("Use a password with at least 6 characters.")},
        {QStringLiteral("AUTH_LINK_INVALID"),
         QStringLiteral("That sign-in link is invalid or expired. Request a new one.")},
        {QStringLiteral("AUTH_ACCOUNT_EXISTS"),
         QStringLiteral("This email is already used with another sign-in method.")},
        {QStringLiteral("AUTH_PROVIDER_DISABLED"),
         QStringLiteral("This sign-in method is not enabled. Try email and password.")},
        {QStringLiteral("AUTH_IN_PROGRESS"), QStringLiteral("Sign-in is already in progress.")},
        // ─── Ariadne's Thread [AT-0196] ─────────────────────
        // What: Map OAuth PKCE failure codes to English UI strings
        // Why:  Mac no longer shows email/password errors for website sign-in
        // Date: 2026-08-27
        // Related: [AT-0193] AuthSession.cpp:completeWebsiteCallback, [AT-0002] ErrorCatalog.cpp
        // ─────────────────────────────────────────────────────
        {QStringLiteral("AUTH_OAUTH_DENIED"), QStringLiteral("Sign-in was canceled.")},
        {QStringLiteral("AUTH_OAUTH_FAILED"),
         QStringLiteral("Could not sign in with seenshot.app. Try again.")},
        {QStringLiteral("AUTH_OAUTH_STATE"),
         QStringLiteral("Could not finish sign-in. Click Sign In again.")},
        {QStringLiteral("LOGIN_ITEM_FAILED"),
         QStringLiteral("Could not change Open at login. Check Login Items in System Settings.")},
        {QStringLiteral("AUTH_CHECK_EMAIL"), QStringLiteral("Check your email to continue.")},
        // ─── Ariadne's Thread [AT-0189] ─────────────────────
        // What: Map AUTH_DISPOSABLE_EMAIL to the official blocklist message
        // Why:  Same English string as seenshot-web and the disposable-email-domains README
        // Date: 2026-08-27
        // Related: [AT-0188] FirebaseAuthClient.cpp:allowPermanentEmail, seenshot-web→public/js/auth.js
        // ─────────────────────────────────────────────────────
        {QStringLiteral("AUTH_DISPOSABLE_EMAIL"),
         QStringLiteral("Please enter your permanent email address.")},
        {QStringLiteral("UPDATE_FAILED"),
         QStringLiteral("Could not download or install the update. Try again.")},
        {QStringLiteral("UPDATE_PERSIST_FAILED"),
         QStringLiteral("Could not save this screenshot before updating. Save a local PNG, then tap Update again.")},
        {QStringLiteral("UNKNOWN_ERROR"),
         QStringLiteral("Something went wrong. Try again.")},
    };

    const QString text = kMessages.value(code, kMessages.value(QStringLiteral("UNKNOWN_ERROR")));
    qWarning() << "ErrorCatalog: resolve code=" << code << " message=" << text;
    return text;
}
