# PRD-04: Settings and sign-in

Product requirements for the SeenShot Settings window: capture shortcuts, launch at login, and a real account flow. Stage 1 architecture (cloud, quota, capture, hotkey engine), PRD-02 annotate tools, and PRD-03 photo cutout are unchanged. This document is requirements only. Implementation must follow this document only.

Stage 1 TZ: [SeenShot — полный план (этап 1)](/Users/alexign/.cursor/plans/seenshot_full_architecture_c5152778.plan.md)

PRD-02: [Annotate tools](/Users/alexign/Desktop/seenshot/docs/PRD-02-annotate-tools.md)

PRD-03: [Photo cutout](/Users/alexign/Desktop/seenshot/docs/PRD-03-photo-cutout.md)

The product gap: Settings shows Sign Out / Upgrade / Export / Delete while signed out, and sign-in is email+password only with no reset, no email link, no Google, and no launch-at-login.

UI strings: English. Logs: English.

---

## Now / must become

| Area | Now | Must become |
| --- | --- | --- |
| Layout | One form: email, password, two hotkeys, then Sign In + Sign Out + Upgrade + Export + Delete always visible. | Capture always. Account is signed-out **or** signed-in. |
| Launch at login | Missing. | Checkbox **Open SeenShot at login**. Default on after install. `SMAppService.mainApp`. |
| Email / password | `accounts:signInWithPassword` only. | Sign In + Create account on one form. |
| Password reset | Missing. | `accounts:sendOobCode` `PASSWORD_RESET`. App shows Check your email. |
| Email link | Missing. | `EMAIL_SIGNIN` + `accounts:signInWithEmailLink`. Scheme `seenshot://`. |
| Google | Missing. | `ASWebAuthenticationSession` then `accounts:signInWithIdp`. |
| Profile | `uid` string. | Email from Identity Toolkit. Plan + quota after sign-in only. |

---

## 1. Capture is always on the Settings window

- **Full Screen Shot** and **Path Screen Shot** stay. Same `QKeySequenceEdit` + `LocalStore` + `hotkeysChanged`.
- Checkbox **Open SeenShot at login** under the hotkeys.
- Official API only: `SMAppService.mainApp` (`ServiceManagement`). The checkbox is `status`, not a second QSettings flag.
- After install, launch at login is on. `Application::start` registers the login item if it is not already enabled. Do not ask the user.
- Uncheck calls `unregister`. System refusal: checkbox off, ErrorCatalog, app stays running. Capture still works.

---

## 2. Account has two states

Signed out — only sign-in:

- Continue with Google
- Email + Password
- Sign In and Create account (one form, two buttons)
- Forgot password
- Email me a sign-in link

Do not show and do not call: Sign Out, Upgrade to Pro, Export my data, Delete account.

Signed in — profile:

- Email (not raw uid). Persist email in Keychain with the refresh token.
- Plan + cloud used, same `CloudClient::fetchQuota` as today.
- Sign Out, Upgrade to Pro, Export my data, Delete account. Same `CloudClient` methods. Do not add a second checkout / export / delete path.

Local Save without an account stays. Share Link while signed out opens a modal with the same sign-in form (`AccountSignInPanel`) and the English line **Sign in to share links.** After a session exists, Share continues. Closing the modal does not share. Offline after sign-in still uses `OFFLINE_CLOUD_UNAVAILABLE`.

---

## 3. Email, password, reset, email link

Extend the existing Identity Toolkit REST client and the existing `AuthSession::persist`. Do not add Firebase C++ or JS SDK. Do not add a second session store.

- Sign In: `accounts:signInWithPassword` (already present).
- Create account: `accounts:signUp`.
- Forgot password: `accounts:sendOobCode` `PASSWORD_RESET`. Password is changed on the Firebase hosted page. The app only shows Check your email.
- Email link: `accounts:sendOobCode` `EMAIL_SIGNIN`, then `accounts:signInWithEmailLink`. After send, store the email in `QSettings` (`pendingSignInEmail`). Continue URL must be an authorized Firebase domain. Custom scheme `seenshot://` in Info.plist (`CFBundleURLTypes`). Incoming URL via Apple Event / Qt URL handler. If Settings is closed, open it and finish sign-in.

Empty email / empty password on password paths: ErrorCatalog, no network.

One in-flight auth request. Buttons disabled. A second Sign In / Google / link must not start in parallel.

Map Identity Toolkit errors to their own ErrorCatalog codes. Do not dump every failure into `AUTH_REFRESH_FAILED`.

---

## 4. Google

One social provider: Google.

- Button opens `ASWebAuthenticationSession` (AuthenticationServices).
- Authorization code + PKCE against Google OAuth, then `id_token` into `accounts:signInWithIdp` `providerId=google.com`.
- Same `FirebaseTokens` and `AuthSession::persist`.
- User cancel: log only. No panic dialog.
- Client id: Info.plist / Config, same pattern as `firebaseApiKey`. Firebase Console must have Google enabled.

---

## 5. Quality of the window

One Settings window. No second app. No WebView wrapping FirebaseUI.

- Signed out: sign-in form. After success the form is gone and the profile is shown.
- Sign In and Create account stay on one form.
- After reset or email-link send: an English status line, not a blank window.
- Log every step: login-item register/unregister, send oob, Google session, URL callback, persist.

---

## 6. Out of scope for this PRD

- Apple, X/Twitter, GitHub, Phone, Yahoo, Microsoft.
- In-app “new password” form after reset.
- Separate account-linking UI.
- Changing Stage 1 cloud / quota / hotkey engine.
- PRD-02 / PRD-03 editor behavior.

Do not add a second auth stack next to `FirebaseAuthClient` / `AuthSession`.

---

## Corner cases

- Email link arrives while the app is quit: `seenshot://` must launch the agent (`LSUIElement`).
- Email link arrives after the user already signed in with a password: drop pending email. Do not replace the session.
- Same email already used with Google, user taps Create account with a password (or the reverse): show `EMAIL_IN_USE` / account-exists. Do not fail silently.
- Login Items off in System Settings: checkbox off. Capture still works.
- Offline: do not fake a successful password / Google / link sign-in. Local capture without an account is fine.
- All UI labels: English. All logs: English.
