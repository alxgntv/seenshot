<!-- ─── Ariadne's Thread [AT-0180] ─────────────────────
  What: Document macOS Screen Recording TCC for SeenShot copies
  Why:  Settings switch on while CGPreflight is false is two signed copies, not a broken toggle
  Date: 2026-08-26
  Related: [AT-0176] Application.cpp:ensureScreenRecording, [AT-0179] CMakeLists.txt
─────────────────────────────────────────────────────── -->

# macOS Screen Recording for SeenShot

## Symptom

Hotkey after quit and reopen shows the system alert **Open System Settings**. Screen Recording already has SeenShot switched **on**. Capture still does not start.

## Cause

macOS Screen Recording (TCC) is **not** keyed by the display name “SeenShot”. It is keyed by the **code signing designated requirement** of the running `.app`:

- bundle id `com.seenshot.app`
- plus the **Team ID** (Developer ID) or the **Apple Development certificate CN**

System Settings lists every copy as **SeenShot**. The switch that is on can belong to a **different binary** than the one that just launched.

On this project those copies were:

| Path | Signature | Team / identity |
| --- | --- | --- |
| `/Applications/SeenShot.app` | Developer ID Application | `V2UJNP6U9G` |
| `build/SeenShot.app` | Apple Development | `Apple Development: alexign90@icloud.com (N9AN38ACU9)` |

`CGPreflightScreenCaptureAccess()` reports the running process. It stays `false` when the switch in Settings is bound to the other identity. `CGRequestScreenCaptureAccess()` then shows **Open System Settings** again. The user sees the switch already on and has nowhere to go.

TCC also does not attach to the **already running** process after the user flips the switch. The app must quit and start again.

## What the app does now

1. `CGPreflightScreenCaptureAccess()` (CoreGraphics).
2. If that is false, `+[SCShareableContent getShareableContentWithCompletionHandler:]` (ScreenCaptureKit). That call is the same permission the capture path uses.
3. `CGRequestScreenCaptureAccess()` only on the first registration, so the system sheet does not loop on every launch.
4. If this copy is still not authorized, SeenShot shows its own dialog with **this copy’s path**, tells the user to turn the switch **off then on**, and offers **Open System Settings** / **Restart SeenShot**.

Do not gate capture on `CGPreflight` alone. Do not call `CGRequestScreenCaptureAccess` on every launch.

## Rules for local development

1. **Run one copy.** Quit SeenShot from the menu bar (it is `LSUIElement`; closing a window does not quit). Then launch either `/Applications/SeenShot.app` **or** `build/SeenShot.app`, not both.
2. **Sign local builds with the same identity as the shipped app.** CMake picks `Developer ID Application` from the keychain when `CODESIGN_IDENTITY` is unset, matching `packaging/macos/package_sparkle.sh`. Override only when you intend a different TCC identity:
   ```bash
   export CODESIGN_IDENTITY="Developer ID Application: Pavel Kaloshin (V2UJNP6U9G)"
   cmake -S . -B build
   cmake --build build
   ```
3. After a **team or certificate change**, the Settings switch is stale. Turn **SeenShot** off, then on, then restart the app. The dialog shows the path of the copy that needs the switch.
4. Launch Services often prefers `/Applications/SeenShot.app`. To run the build folder copy:
   ```bash
   killall SeenShot || true
   open -n "$PWD/build/SeenShot.app"
   ```
5. Confirm which binary is running and how it is signed:
   ```bash
   pgrep -afl SeenShot
   codesign -dv --verbose=4 /path/to/SeenShot.app
   codesign -d -r- /path/to/SeenShot.app
   ```
   Same `TeamIdentifier` / designated requirement ⇒ same Screen Recording switch. Different requirement ⇒ two switches, one name.

## If you are still stuck

Reset only this bundle, then launch **one** copy and grant Screen Recording again:

```bash
tccutil reset ScreenCapture com.seenshot.app
killall SeenShot || true
```

Do not call `tccutil` from the app. Apple treats that pattern as TCC bypass.

## Official APIs (do not replace)

- [`CGPreflightScreenCaptureAccess`](https://developer.apple.com/documentation/coregraphics/cgpreflightscreencaptureaccess())
- [`CGRequestScreenCaptureAccess`](https://developer.apple.com/documentation/coregraphics/cgrequestscreencaptureaccess())
- [`+[SCShareableContent getShareableContentWithCompletionHandler:]`](https://developer.apple.com/documentation/screencapturekit/scshareablecontent/getshareablecontentwithcompletionhandler:)
- `NSScreenCaptureUsageDescription` in `packaging/macos/Info.plist`

## Corner cases

- **First capture after a fresh install** may still show the system sheet once so this binary appears in the Screen Recording list. After that, a denied copy sees the in-app dialog, not a loop of **Open System Settings**.
- **Revoking** Screen Recording later is not silent: the next hotkey shows the in-app dialog.
- **Rebuild without a stable identity** (ad-hoc) creates a new TCC identity every sign. Do not ship or daily-drive an ad-hoc `SeenShot.app`.
- **Sparkle** replaces `/Applications/SeenShot.app` with the same Developer ID requirement. Users who only use that copy do not need to flip the switch on each update.
- Closing the annotate window does not quit. If two copies start, you will keep hitting the “switch already on” state.
