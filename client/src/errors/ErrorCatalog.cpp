#include "errors/ErrorCatalog.h"

#include <QDebug>
#include <QHash>

// ─── Ariadne's Thread [AT-0002] ─────────────────────
// What: Map error codes to English UI strings
// Why:  One catalog for dialogs and API payloads
// Date: 2026-08-25
// Related: [AT-0001] ErrorCatalog.h
// ─────────────────────────────────────────────────────
QString ErrorCatalog::message(const QString &code)
{
    static const QHash<QString, QString> kMessages = {
        {QStringLiteral("OFFLINE_CLOUD_UNAVAILABLE"),
         QStringLiteral("You are offline. Use Save to keep a local PNG. Cloud save and sharing need an internet connection.")},
        {QStringLiteral("SCREEN_RECORDING_DENIED"),
         QStringLiteral("Turn on Screen Recording for SeenShot, then quit and open the app again. The annotate editor opens after a successful capture.")},
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
         QStringLiteral("macOS Keychain is unavailable. SeenShot cannot store your sign-in.")},
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
         QStringLiteral("This sign-in method is not enabled. Try email or Google.")},
        {QStringLiteral("AUTH_IN_PROGRESS"), QStringLiteral("Sign-in is already in progress.")},
        {QStringLiteral("GOOGLE_SIGN_IN_CANCELLED"), QStringLiteral("Google sign-in was cancelled.")},
        {QStringLiteral("GOOGLE_SIGN_IN_UNAVAILABLE"),
         QStringLiteral("Google sign-in is not configured for this build.")},
        {QStringLiteral("LOGIN_ITEM_FAILED"),
         QStringLiteral("Could not change Open at login. Check Login Items in System Settings.")},
        {QStringLiteral("AUTH_CHECK_EMAIL"), QStringLiteral("Check your email to continue.")},
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
