#include "app/MacPermissions.h"

#include <QDebug>
#include <QWidget>

#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

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
    [window setCollectionBehavior:NSWindowCollectionBehaviorCanJoinAllSpaces
                                  | NSWindowCollectionBehaviorFullScreenAuxiliary
                                  | NSWindowCollectionBehaviorTransient];
    [window makeKeyAndOrderFront:nil];
    qInfo() << "MacPermissions: pinCaptureOverlay visible=" << overlay->isVisible()
            << " active=" << overlay->isActiveWindow() << " geo=" << overlay->geometry()
            << " level=" << static_cast<int>([window level])
            << " ignoresMouse=" << static_cast<bool>([window ignoresMouseEvents]);
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

void MacPermissions::openScreenRecordingSettings()
{
    NSURL *url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"];
    const BOOL ok = [[NSWorkspace sharedWorkspace] openURL:url];
    qInfo() << "MacPermissions: open Screen Recording settings ok=" << ok;
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
            << " mainThread=" << [NSThread isMainThread];
    return ok;
}

bool MacPermissions::requestScreenRecording()
{
    const bool ok = CGRequestScreenCaptureAccess();
    qInfo() << "MacPermissions: CGRequestScreenCaptureAccess=" << ok
            << " mainThread=" << [NSThread isMainThread];
    return ok;
}

// ─── Ariadne's Thread [AT-0061] ─────────────────────
// What: Open Privacy Camera in System Settings and yield activation to it
// Why:  Legacy pane URL plus a stay-on-top modal left the user on OK without Camera TCC
// Date: 2026-08-25
// Related: [AT-0035] MacPermissions.mm, [AT-0061] AnnotateWindow.cpp:startPhotoCycle
// ─────────────────────────────────────────────────────
static void activateOpenedSettings(NSRunningApplication *app)
{
    if (app == nil) {
        qWarning() << "MacPermissions: Camera settings app is nil";
        return;
    }
    qInfo() << "MacPermissions: yield activation to Camera settings bundle="
            << QString::fromNSString(app.bundleIdentifier ?: @"");
    [NSApp yieldActivationToApplication:app];
    const BOOL ok = [app activateFromApplication:[NSRunningApplication currentApplication]
                                         options:NSApplicationActivateAllWindows];
    qInfo() << "MacPermissions: Camera settings activateFromApplication=" << ok << " active=" << app.active;
}

void MacPermissions::openCameraSettings()
{
    NSArray<NSString *> *urls = @[
        @"x-apple.systempreferences:com.apple.settings.PrivacySecurity.extension?Privacy_Camera",
        @"x-apple.systempreferences:com.apple.preference.security?Privacy_Camera"
    ];
    NSWorkspaceOpenConfiguration *config = [NSWorkspaceOpenConfiguration configuration];
    config.activates = YES;
    NSURL *primary = [NSURL URLWithString:urls[0]];
    qInfo() << "MacPermissions: opening Camera settings primary url=" << QString::fromNSString(urls[0]);
    [[NSWorkspace sharedWorkspace] openURL:primary
                             configuration:config
                         completionHandler:^(NSRunningApplication *app, NSError *error) {
                             dispatch_async(dispatch_get_main_queue(), ^{
                                 if (error == nil && app != nil) {
                                     activateOpenedSettings(app);
                                     return;
                                 }
                                 qWarning() << "MacPermissions: primary Camera settings failed"
                                            << (error != nil ? QString::fromNSString(error.localizedDescription)
                                                             : QStringLiteral("no app"));
                                 NSURL *fallback = [NSURL URLWithString:urls[1]];
                                 qInfo() << "MacPermissions: opening Camera settings fallback url="
                                         << QString::fromNSString(urls[1]);
                                 [[NSWorkspace sharedWorkspace] openURL:fallback
                                                          configuration:config
                                                      completionHandler:^(NSRunningApplication *app2, NSError *error2) {
                                                          dispatch_async(dispatch_get_main_queue(), ^{
                                                              if (error2 != nil || app2 == nil) {
                                                                  qWarning()
                                                                      << "MacPermissions: fallback Camera settings failed"
                                                                      << (error2 != nil
                                                                              ? QString::fromNSString(error2.localizedDescription)
                                                                              : QStringLiteral("no app"));
                                                                  return;
                                                              }
                                                              activateOpenedSettings(app2);
                                                          });
                                                      }];
                             });
                         }];
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
