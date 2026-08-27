#include "app/MacPermissions.h"

#include <QCoreApplication>
#include <QDebug>
#include <QString>
#include <QWidget>

#include <atomic>
#include <memory>

#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <Security/Security.h>

void MacPermissions::activateApp()
{
    [NSApp activateIgnoringOtherApps:YES];
    qInfo() << "MacPermissions: activateIgnoringOtherApps";
}

// ─── Ariadne's Thread [AT-0118] ─────────────────────
// What: Pin region overlay as key window above others
// Why:  LSUIElement Tool window stayed invisible; hotkey left capture stuck
// Date: 2026-08-26
// Related: [AT-0010] RegionPicker.cpp, [AT-0119] Application.cpp:beginCapture
// ─────────────────────────────────────────────────────
// ─── Ariadne's Thread [AT-0163] ─────────────────────
// What: Pin the region overlay and set NSWindow movable / movableByWindowBackground off
// Why:  Drag on the first pixel of the rubber moved the whole overlay on macOS
// Date: 2026-08-26
// Related: [AT-0118] MacPermissions.mm:pinCaptureOverlay, [AT-0010] RegionPicker.cpp
// ─────────────────────────────────────────────────────
void MacPermissions::pinCaptureOverlay(QWidget *overlay)
{
    if (!overlay) {
        qWarning() << "MacPermissions: pinCaptureOverlay null";
        return;
    }
    overlay->winId();
    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(overlay->winId());
    NSWindow *window = view.window;
    if (window == nil) {
        qWarning() << "MacPermissions: pinCaptureOverlay no NSWindow visible=" << overlay->isVisible();
        return;
    }
    [window setLevel:CGShieldingWindowLevel()];
    [window setIgnoresMouseEvents:NO];
    [window setMovable:NO];
    [window setMovableByWindowBackground:NO];
    [window setCollectionBehavior:NSWindowCollectionBehaviorCanJoinAllSpaces
                                  | NSWindowCollectionBehaviorFullScreenAuxiliary
                                  | NSWindowCollectionBehaviorTransient];
    [window makeKeyAndOrderFront:nil];
    qInfo() << "MacPermissions: pinCaptureOverlay visible=" << overlay->isVisible()
            << " active=" << overlay->isActiveWindow() << " geo=" << overlay->geometry()
            << " level=" << static_cast<int>([window level])
            << " ignoresMouse=" << static_cast<bool>([window ignoresMouseEvents])
            << " movable=" << static_cast<bool>([window isMovable])
            << " movableByBackground=" << static_cast<bool>([window isMovableByWindowBackground]);
}

// ─── Ariadne's Thread [AT-0126] ─────────────────────
// What: Pin the camera pip as a floating Tool window without taking key
// Why:  LSUIElement hid Qt::Tool; shielding/makeKey would steal annotate input
// Date: 2026-08-26
// Related: [AT-0118] MacPermissions.mm:pinCaptureOverlay, [AT-0128] AnnotateWindow.cpp:layoutPhotoOverlay
// ─────────────────────────────────────────────────────
void MacPermissions::pinFloatingToolWindow(QWidget *overlay)
{
    if (!overlay) {
        qWarning() << "MacPermissions: pinFloatingToolWindow null";
        return;
    }
    overlay->winId();
    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(overlay->winId());
    NSWindow *window = view.window;
    if (window == nil) {
        qWarning() << "MacPermissions: pinFloatingToolWindow no NSWindow visible=" << overlay->isVisible();
        return;
    }
    [window setLevel:NSFloatingWindowLevel];
    [window setIgnoresMouseEvents:NO];
    [window setCollectionBehavior:NSWindowCollectionBehaviorCanJoinAllSpaces
                                  | NSWindowCollectionBehaviorFullScreenAuxiliary
                                  | NSWindowCollectionBehaviorTransient];
    [window orderFront:nil];
    qInfo() << "MacPermissions: pinFloatingToolWindow visible=" << overlay->isVisible()
            << " geo=" << overlay->geometry() << " level=" << static_cast<int>([window level])
            << " ignoresMouse=" << static_cast<bool>([window ignoresMouseEvents]);
}

// ─── Ariadne's Thread [AT-0177] ─────────────────────
// What: Log bundle path and Team ID for the running copy
// Why:  /Applications (Developer ID V2UJNP6U9G) and build/ (Apple Development) are different TCC identities
// Date: 2026-08-26
// Related: [AT-0176] Application.cpp:ensureScreenRecording, docs/macos-screen-recording.md
// ─────────────────────────────────────────────────────
// ─── Ariadne's Thread [AT-0183] ─────────────────────
// What: Copy Team ID into QString before CFRelease of the signing dictionary
// Why:  MRC left a dangling NSString; probeScreenRecording SIGSEGV in QString::fromNSString
// Date: 2026-08-26
// Related: [AT-0177] MacPermissions.mm:logSigningIdentity, [AT-0178] MacPermissions.mm:probeScreenRecording
// ─────────────────────────────────────────────────────
static void logSigningIdentity(const char *where)
{
    const QString path = QString::fromNSString([NSBundle mainBundle].bundlePath ?: @"");
    QString team;
    SecCodeRef code = NULL;
    const OSStatus selfStatus = SecCodeCopySelf(kSecCSDefaultFlags, &code);
    if (selfStatus == errSecSuccess && code != NULL) {
        CFDictionaryRef info = NULL;
        const OSStatus infoStatus = SecCodeCopySigningInformation(code, kSecCSSigningInformation, &info);
        if (infoStatus == errSecSuccess && info != NULL) {
            const void *teamValue = CFDictionaryGetValue(info, kSecCodeInfoTeamIdentifier);
            if (teamValue != NULL && CFGetTypeID(teamValue) == CFStringGetTypeID()) {
                team = QString::fromNSString((__bridge NSString *)teamValue);
            }
            CFRelease(info);
        } else {
            qWarning() << "MacPermissions: SecCodeCopySigningInformation status=" << static_cast<int>(infoStatus);
        }
        CFRelease(code);
    } else {
        qWarning() << "MacPermissions: SecCodeCopySelf status=" << static_cast<int>(selfStatus);
    }
    qInfo() << "MacPermissions:" << where << " bundle=" << path << " team=" << team
            << " mainThread=" << [NSThread isMainThread];
}

QString MacPermissions::runningAppPath()
{
    return QString::fromNSString([NSBundle mainBundle].bundlePath ?: @"");
}

// ─── Ariadne's Thread [AT-0061] ─────────────────────
// What: Open a Privacy pane in System Settings and yield activation to it
// Why:  Legacy pane URL plus a stay-on-top modal left the user on OK without Camera TCC
// Date: 2026-08-25
// Related: [AT-0035] MacPermissions.mm, [AT-0061] AnnotateWindow.cpp:startPhotoCycle
// ─────────────────────────────────────────────────────
static void activateOpenedSettings(NSRunningApplication *app, const char *label)
{
    if (app == nil) {
        qWarning() << "MacPermissions:" << label << "settings app is nil";
        return;
    }
    qInfo() << "MacPermissions: yield activation to" << label << "settings bundle="
            << QString::fromNSString(app.bundleIdentifier ?: @"");
    [NSApp yieldActivationToApplication:app];
    const BOOL ok = [app activateFromApplication:[NSRunningApplication currentApplication]
                                         options:NSApplicationActivateAllWindows];
    qInfo() << "MacPermissions:" << label << "settings activateFromApplication=" << ok << " active=" << app.active;
}

static void openPrivacyPane(NSArray<NSString *> *urls, const char *label)
{
    NSWorkspaceOpenConfiguration *config = [NSWorkspaceOpenConfiguration configuration];
    config.activates = YES;
    NSURL *primary = [NSURL URLWithString:urls[0]];
    qInfo() << "MacPermissions: opening" << label << "settings primary url=" << QString::fromNSString(urls[0]);
    [[NSWorkspace sharedWorkspace] openURL:primary
                             configuration:config
                         completionHandler:^(NSRunningApplication *app, NSError *error) {
                             dispatch_async(dispatch_get_main_queue(), ^{
                                 if (error == nil && app != nil) {
                                     activateOpenedSettings(app, label);
                                     return;
                                 }
                                 qWarning() << "MacPermissions: primary" << label << "settings failed"
                                            << (error != nil ? QString::fromNSString(error.localizedDescription)
                                                             : QStringLiteral("no app"));
                                 NSURL *fallback = [NSURL URLWithString:urls[1]];
                                 qInfo() << "MacPermissions: opening" << label
                                         << "settings fallback url=" << QString::fromNSString(urls[1]);
                                 [[NSWorkspace sharedWorkspace] openURL:fallback
                                                          configuration:config
                                                      completionHandler:^(NSRunningApplication *app2, NSError *error2) {
                                                          dispatch_async(dispatch_get_main_queue(), ^{
                                                              if (error2 != nil || app2 == nil) {
                                                                  qWarning()
                                                                      << "MacPermissions: fallback" << label
                                                                      << "settings failed"
                                                                      << (error2 != nil
                                                                              ? QString::fromNSString(error2.localizedDescription)
                                                                              : QStringLiteral("no app"));
                                                                  return;
                                                              }
                                                              activateOpenedSettings(app2, label);
                                                          });
                                                      }];
                             });
                         }];
}

void MacPermissions::openScreenRecordingSettings()
{
    logSigningIdentity("openScreenRecordingSettings");
    openPrivacyPane(@[
        @"x-apple.systempreferences:com.apple.settings.PrivacySecurity.extension?Privacy_ScreenCapture",
        @"x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"
    ], "Screen Recording");
}

// ─── Ariadne's Thread [AT-0035] ─────────────────────
// What: Read and request Screen Recording via CoreGraphics
// Why:  lldb showed capture dying after 8s even when TCC is granted; need explicit preflight logs
// Date: 2026-08-25
// Related: [AT-0034] MacPermissions.mm:openScreenRecordingSettings, [AT-0009] ScreenCaptureBackend.mm
// ─────────────────────────────────────────────────────
bool MacPermissions::hasScreenRecording()
{
    const bool ok = CGPreflightScreenCaptureAccess();
    qInfo() << "MacPermissions: CGPreflightScreenCaptureAccess=" << ok
            << " mainThread=" << [NSThread isMainThread]
            << " bundle=" << runningAppPath();
    return ok;
}

// ─── Ariadne's Thread [AT-0178] ─────────────────────
// What: Probe ScreenCaptureKit getShareableContent when CGPreflight is false
// Why:  Official capture API is ground truth; CGPreflight stays false for the other signed copy
// Date: 2026-08-26
// Related: [AT-0035] MacPermissions.mm:hasScreenRecording, [AT-0037] ScreenCaptureBackend.mm:captureRegion
// ─────────────────────────────────────────────────────
bool MacPermissions::probeScreenRecording()
{
    logSigningIdentity("probeScreenRecording");
    if (![NSThread isMainThread]) {
        qWarning() << "MacPermissions: probeScreenRecording refused off main thread";
        return false;
    }
    const auto finished = std::make_shared<std::atomic<bool>>(false);
    const auto ok = std::make_shared<std::atomic<bool>>(false);
    [SCShareableContent getShareableContentWithCompletionHandler:^(
                            SCShareableContent *_Nullable content, NSError *_Nullable error) {
        const bool granted = (error == nil && content != nil && content.displays.count > 0);
        ok->store(granted);
        qInfo() << "MacPermissions: probeShareableContent granted=" << granted
                << " displays=" << (content != nil ? static_cast<int>(content.displays.count) : -1)
                << " error=" << (error != nil ? QString::fromNSString(error.localizedDescription) : QString())
                << " domain=" << (error != nil ? QString::fromNSString(error.domain) : QString())
                << " code=" << (error != nil ? static_cast<int>(error.code) : 0)
                << " callbackMainThread=" << [NSThread isMainThread];
        finished->store(true);
    }];
    const NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:3.0];
    while (!finished->load() && [deadline timeIntervalSinceNow] > 0) {
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
    }
    const bool granted = finished->load() && ok->load();
    qInfo() << "MacPermissions: probeScreenRecording finished=" << finished->load() << " granted=" << granted;
    return granted;
}

bool MacPermissions::requestScreenRecording()
{
    logSigningIdentity("requestScreenRecording");
    const bool ok = CGRequestScreenCaptureAccess();
    qInfo() << "MacPermissions: CGRequestScreenCaptureAccess=" << ok
            << " mainThread=" << [NSThread isMainThread];
    return ok;
}

// ─── Ariadne's Thread [AT-0172] ─────────────────────
// What: Relaunch the same .app via NSWorkspace after Screen Recording toggle
// Why:  TCC does not apply to the running process; custom Quit dialog was extra
// Date: 2026-08-26
// Related: [AT-0035] MacPermissions.mm:requestScreenRecording, [AT-0172] Application.cpp:ensureScreenRecording
// ─────────────────────────────────────────────────────
void MacPermissions::relaunchApp()
{
    NSURL *url = [NSBundle mainBundle].bundleURL;
    qInfo() << "MacPermissions: relaunchApp url=" << QString::fromNSString(url.path ?: @"");
    NSWorkspaceOpenConfiguration *config = [NSWorkspaceOpenConfiguration configuration];
    config.createsNewApplicationInstance = YES;
    [[NSWorkspace sharedWorkspace] openApplicationAtURL:url
                                          configuration:config
                                      completionHandler:^(NSRunningApplication *app, NSError *error) {
                                          dispatch_async(dispatch_get_main_queue(), ^{
                                              if (error != nil) {
                                                  qWarning()
                                                      << "MacPermissions: relaunch failed"
                                                      << QString::fromNSString(error.localizedDescription);
                                              } else {
                                                  qInfo() << "MacPermissions: relaunch started pid="
                                                          << (app != nil ? static_cast<int>(app.processIdentifier)
                                                                         : 0);
                                              }
                                              qApp->quit();
                                          });
                                      }];
}

void MacPermissions::openCameraSettings()
{
    openPrivacyPane(@[
        @"x-apple.systempreferences:com.apple.settings.PrivacySecurity.extension?Privacy_Camera",
        @"x-apple.systempreferences:com.apple.preference.security?Privacy_Camera"
    ], "Camera");
}

bool MacPermissions::hasCamera()
{
    const AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
    const bool ok = (status == AVAuthorizationStatusAuthorized);
    qInfo() << "MacPermissions: camera status=" << static_cast<int>(status) << " authorized=" << ok
            << " mainThread=" << [NSThread isMainThread];
    return ok;
}

// ─── Ariadne's Thread [AT-0062] ─────────────────────
// What: Always call requestAccessForMediaType unless already Authorized
// Why:  Early Denied return skipped the system Camera sheet, so SeenShot never entered TCC
// Date: 2026-08-25
// Related: [AT-0062] packaging/macos/SeenShot.entitlements, [AT-0061] AnnotateWindow.cpp:startPhotoCycle
// ─────────────────────────────────────────────────────
void MacPermissions::requestCamera(const std::function<void(bool)> &done)
{
    const AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
    qInfo() << "MacPermissions: requestCamera status=" << static_cast<int>(status);
    if (status == AVAuthorizationStatusAuthorized) {
        done(true);
        return;
    }
    std::function<void(bool)> callback = done;
    qInfo() << "MacPermissions: calling requestAccessForMediaType AVMediaTypeVideo";
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                             completionHandler:^(BOOL granted) {
                                 dispatch_async(dispatch_get_main_queue(), ^{
                                     const AVAuthorizationStatus after =
                                         [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
                                     qInfo() << "MacPermissions: camera request granted=" << granted
                                             << " statusAfter=" << static_cast<int>(after);
                                     callback(granted);
                                 });
                             }];
}
