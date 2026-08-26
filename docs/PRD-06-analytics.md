# PRD-06: Analytics and crash reporting

Product requirements for SeenShot usage analytics and crash reports via the official [posthog-cpp](https://github.com/baskl-ai/posthog-cpp) 1.7.1 SDK. Stage 1 architecture, PRD-02 annotate tools, PRD-03 photo cutout, PRD-04 settings/auth, and PRD-05 auto-update are unchanged. This document is requirements only. Implementation must follow this document only.

Stage 1 TZ: [SeenShot — полный план (этап 1)](/Users/alexign/.cursor/plans/seenshot_full_architecture_c5152778.plan.md)

PRD-02: [Annotate tools](/Users/alexign/Desktop/seenshot/docs/PRD-02-annotate-tools.md)

PRD-03: [Photo cutout](/Users/alexign/Desktop/seenshot/docs/PRD-03-photo-cutout.md)

PRD-04: [Settings and sign-in](/Users/alexign/Desktop/seenshot/docs/PRD-04-settings-auth.md)

PRD-05: [Auto-update](/Users/alexign/Desktop/seenshot/docs/PRD-05-auto-update.md)

The product gap: local `Logger` writes English lines to disk. Nothing is sent to PostHog. Crashes leave no `$exception` for Error Tracking.

UI strings: English. Logs: English.

---

## Now / must become

| Area | Now | Must become |
| --- | --- | --- |
| SDK | None. | One `PostHog::Client` from posthog-cpp 1.7.1. FetchContent pin `v1.7.1`. |
| HTTP | Qt Network for Firebase and the SeenShot API. | SDK libcurl only. Do not send PostHog events through `QNetworkAccessManager`. |
| Events | None. | Six success events: `app_started`, `sign_in`, `capture`, `save`, `share`, `update`. |
| Crash | No signal handler. | `installCrashHandler()` default dir for `SeenShot`. Pending crash is sent on the next launch. |
| Identity | — | SDK machine id. Do not send email, uid, file path, or share URL. |
| Opt-out | — | Official `~/.posthog_optout`. No Settings checkbox. |

---

## 1. One client

One `Analytics` wrapper owns one `PostHog::Client`. Do not construct a second client.

Official cycle:

- `Config.apiKey` / `Config.host` from env then Info.plist (`SEENSHOT_POSTHOG_API_KEY`, `SEENSHOT_POSTHOG_HOST`, keys `posthogApiKey`, `posthogHost`). Default host: `https://eu.i.posthog.com`.
- `appName` = `SeenShot`. `appVersion` = `QCoreApplication::applicationVersion`. `distinctId` empty.
- Empty api key: do not call `initialize`. `track` is a no-op. The app stays usable.
- After `Logger::install`: `initialize`, `installCrashHandler()`, `setCrashMetadata` with `app_version`, `setLogFile(Logger::filePath(), 50)`.
- `shutdown()` after `QApplication::exec` and before `qApp->quit` in `Application::confirmQuit`.

Do not use `Client::log` or `ClientLogSink`. Local logs stay in `Logger`.

CMake: `find_package(CURL REQUIRED)` before FetchContent. `POSTHOG_BUILD_TESTS` off. Do not set `POSTHOG_USE_CURL OFF`.

---

## 2. Events

Track only after success. Properties are strings or numbers. No email, uid, file path, or share URL.

| Event | When | Properties |
| --- | --- | --- |
| `app_started` | After `initialize` | `version` |
| `sign_in` | After `AuthSession::finishTokens` persist | `method`: `password` / `signup` / `link` / `google` |
| `capture` | After a successful region or full-screen capture | `kind`: `region` / `fullscreen` |
| `save` | After `AnnotateWindow::saveLocal` writes the PNG | none |
| `share` | After `AnnotateWindow::share` publishes | none |
| `update` | Sparkle offer / download / install | `stage`: `offer` / `download` / `install` |

Each `track` logs the event name and property keys (not PII values) and sets crash metadata `last_action` to the event name.

---

## 3. Crash reporting

`installCrashHandler()` with the SDK default directory for `appName` SeenShot:

`~/Library/Application Support/SeenShot/CrashReports`

Do not install a second signal handler. Do not invent a parallel crash file format.

On the next launch, the SDK sends `$exception` if `pending_crash.txt` exists.

`setLogFile` attaches the last 50 lines of `seenshot.log`.

---

## 4. Out of scope

- Settings toggle for analytics.
- `trackException` on every `ErrorCatalog` error.
- PostHog identify / alias to Firebase uid.
- A second HTTP client for `/batch`.
- Website privacy copy (product page, not this app).

---

## 5. Corner cases

- Missing `phc_` key: analytics off, crashes are not sent.
- `~/.posthog_optout` present: SDK sets `enabled = false`.
- libcurl missing at configure: CMake fails (`REQUIRED`).
- Offline: SDK queue. `shutdown` flushes. Events may not arrive.
- Crash stacks are addresses. Symbols need the same binary / dSYM and SDK `scripts/symbolize.py`.
- Machine id is a SHA256 of the MAC (SDK default). Still do not send email or uid.
