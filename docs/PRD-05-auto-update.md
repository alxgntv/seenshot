# PRD-05: Auto-update from the website

Product requirements for SeenShot updates outside the Mac App Store. The app is downloaded from the SeenShot website. After that, a new build must reach installed copies without a second download from the site. Stage 1 architecture, PRD-02 annotate tools, PRD-03 photo cutout, and PRD-04 settings/auth are unchanged. This document is requirements only. Implementation must follow this document only.

Stage 1 TZ: [SeenShot — полный план (этап 1)](/Users/alexign/.cursor/plans/seenshot_full_architecture_c5152778.plan.md)

PRD-02: [Annotate tools](/Users/alexign/Desktop/seenshot/docs/PRD-02-annotate-tools.md)

PRD-03: [Photo cutout](/Users/alexign/Desktop/seenshot/docs/PRD-03-photo-cutout.md)

PRD-04: [Settings and sign-in](/Users/alexign/Desktop/seenshot/docs/PRD-04-settings-auth.md)

The product gap: Sparkle is already linked (`SparkleUpdater::start`, `SUFeedURL` `https://updates.seenshot.com/appcast.xml`, `SUEnableAutomaticChecks`). `SUPublicEDKey` is empty, so checks cannot be trusted. The UI is Sparkle’s own `SPUStandardUserDriver` windows, not the annotate screen. Install relaunches the process. `LocalStore` has no editor session. The open screenshot and its objects are lost.

UI strings: English. Logs: English.

---

## Now / must become

| Area | Now | Must become |
| --- | --- | --- |
| Channel | Website download only. No App Store. Feed URL already in Info.plist. | Same feed. Same Sparkle. New version = new archive + appcast on `updates.seenshot.com`. |
| Trust | `SUPublicEDKey` empty. `startUpdater` can fail. | Official Sparkle EdDSA public key in Info.plist. Appcast items signed with the matching private key. |
| Check | `SUEnableAutomaticChecks` on. `SPUUpdater` + `SPUStandardUserDriver`. | Keep automatic checks. Do not add a second HTTP updater. |
| Offer | Sparkle system windows. | One card on the annotate screen: copy + **Update**. |
| Install | Sparkle window, then relaunch. Open editor is gone. | Same card shows a progress bar. Persist the open editor, then install and relaunch, then restore that editor. |
| Silent install | Not configured. | Do not set `SUAutomaticallyUpdate`. A check may run in the background. Install starts only from **Update**. |

---

## 1. One updater: the Sparkle already in the app

Extend `SparkleUpdater`. Do not add a parallel download/install stack. Do not wrap a WebView. Do not use the Mac App Store.

Official Sparkle only:

- `SPUUpdater` (already started from `main.cpp`)
- Custom `SPUUserDriver` instead of `SPUStandardUserDriver`
- Feed already in Info.plist / `Config::sparkleFeedUrl()`: `https://updates.seenshot.com/appcast.xml`
- `SUEnableAutomaticChecks` stays true
- `SUAutomaticallyUpdate` stays off

`showUpdatePermissionRequest:reply:`: reply once with automatic checks on and automatic download/install off. Do not show Sparkle’s permission alert.

Background check (`checkForUpdatesInBackground`) finds a newer `SUAppcastItem`. User-visible UI is only the annotate card below.

---

## 2. Publish a version (website + feed)

The first copy still comes from the website. Every later copy comes from the same Sparkle feed.

For each release:

- Raise `CFBundleShortVersionString` and `CFBundleVersion` in `packaging/macos/Info.plist`.
- Sign the `.app` with the existing Apple Development identity and `packaging/macos/SeenShot.entitlements`.
- Upload a Sparkle archive (zip of the `.app`) and an `appcast.xml` to `https://updates.seenshot.com/`.
- Sign the enclosure with Sparkle’s official EdDSA tools. Put the public key in `SUPublicEDKey`. An empty key is forbidden.

The website download and the feed enclosure are the same channel. Do not invent a second “latest.dmg” installer path inside the app.

---

## 3. The offer lives on the screenshot screen

When Sparkle calls `showUpdateFoundWithAppcastItem:state:reply:`, the user sees a card **on `AnnotateWindow`**, centered over the shot:

- English: **A new update is available.**
- One primary button: **Update**.

No second app window. No Sparkle alert. The editor stays open. The shot stays on the scene. Tools, undo, Photo, Save, Share, Settings, capture hotkeys stay as they are.

The card is a chrome overlay. Clicks on the card hit the card. Clicks on the shot and the toolbar still work. The user can finish the current screenshot without pressing **Update**.

If Sparkle finds an update while `AnnotateWindow` is not open (tray only, Settings, region picker, first-run): do not show a desktop alert. Keep the found update. Show the same card when the next `AnnotateWindow` opens.

`SPUUserUpdateChoiceSkip` is not used. There is no Skip button.

If the user does not press **Update**, reply `SPUUserUpdateChoiceDismiss` when the editor closes. Sparkle may offer again on a later check / later editor. The shot is not discarded.

---

## 4. One button: download, progress, install

**Update** replies `SPUUserUpdateChoiceInstall`.

The same card replaces the button with a native `QProgressBar` (determinate when Sparkle gives a length):

- `showDownloadInitiatedWithCancellation:`
- `showDownloadDidReceiveExpectedContentLength:`
- `showDownloadDidReceiveDataOfLength:`
- `showDownloadDidStartExtractingUpdate` / `showExtractionReceivedProgress:`

English status on the card while that runs (download / extract). Logs at every Sparkle callback: version, expected bytes, received bytes, extract progress, errors. Do not log the EdDSA private key.

`showReadyToInstallAndRelaunch:`: persist the open editor first (section 5), then reply `SPUUserUpdateChoiceInstall`. Sparkle replaces the `.app` and relaunches.

Download / extract / install error: `showUpdaterError:acknowledgement:`. ErrorCatalog. Card can offer **Update** again. The editor and the shot stay. Do not close `AnnotateWindow`. Do not quit.

`informationOnlyUpdate`: no download. Log. No card that claims the app will install.

---

## 5. Happy path must survive relaunch

Sparkle relaunch starts a new process. That is not allowed to wipe the current screenshot.

Before the install+relaunch reply, persist the open editor into `LocalStore` (same store, no second draft database):

- The current shot image
- Every annotation object on the shot (PRD-02 / PRD-03 items already on the scene)
- Canvas chrome already on that editor (background preset, corner radius, shadow)

On `Application::start`, if a persisted editor exists, open `AnnotateWindow` with that session and clear the persist. The user sees the same shot and the same objects. Auth session is already Keychain (`AuthSession`). Do not force sign-in again.

If persist fails, do not reply install. ErrorCatalog. Editor stays. User can Save locally and try **Update** again.

Photo countdown / live camera pip in progress: wait until that cycle ends (success, fail, or Escape) before install+relaunch. Do not cut a Photo cycle in half.

Region picker / full-screen capture in progress: do not install+relaunch until that capture has finished or cancelled and, if an editor opened, section 3 applies.

Quit from the tray while a download is in flight: persist if an editor is open, then allow Sparkle to finish on quit. Next launch restores the editor if persist was written.

---

## 6. Out of scope for this PRD

- Mac App Store, TestFlight, Homebrew, a second in-app downloader.
- Windows / Linux.
- Silent install while the user is in the editor (`SUAutomaticallyUpdate`).
- A Settings “Check for Updates” row, a tray “Update” item, or a changelog WebView.
- Changing Stage 1 cloud / quota / hotkeys, PRD-02 tools, PRD-03 Photo, PRD-04 account.

Do not add a second updater next to `SparkleUpdater` / `SPUUpdater`.

---

## Corner cases

- Latest version already installed: no card.
- Offline / feed unreachable: log, no fake “you are up to date” success, no card.
- Empty or wrong `SUPublicEDKey`: do not start a user-visible update. Log. Editor stays.
- Two checks must not run two cards. One in-flight Sparkle session (`sessionInProgress`).
- Critical update (`criticalUpdate`): same card, same persist rules. Still not a Sparkle alert.
- Admin / authorization sheet from Sparkle to replace the `.app`: native Sparkle. Shot stays persisted before relaunch.
- User closes the editor during download: persist if needed, dismiss the card, do not delete the downloaded update. Next editor can continue (`SPUUpdateStateDownloaded` / installing).
- User starts a new capture while a card is up: existing one-editor rule (`m_editor`). Do not open a second annotate window. Do not drop the persisted-or-open session to show a new shot unless the current editor is closed as today.
- After restore, the new binary is running. One restore only. Do not re-open a stale persist on the next cold start.
- All UI labels: English. All logs: English.
