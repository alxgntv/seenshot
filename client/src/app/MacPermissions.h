#pragma once

#include <QString>
#include <QUrl>
#include <functional>

class QWidget;

class MacPermissions {
public:
    static void activateApp();
    // ─── Ariadne's Thread [AT-0209] ─────────────────────
    // What: Open http(s) URLs in the default browser via NSWorkspace
    // Why:  Share must land on /screenshot/{id} in the same browser as website OAuth
    // Date: 2026-08-27
    // Related: [AT-0201] MacOAuthClient.mm:start, [AT-0210] AnnotateWindow.cpp:share
    // ─────────────────────────────────────────────────────
    static bool openDefaultBrowser(const QUrl &pageUrl);
    static void pinCaptureOverlay(QWidget *overlay);
    static void pinFloatingToolWindow(QWidget *overlay);
    static void openScreenRecordingSettings();
    static void openCameraSettings();
    static bool hasScreenRecording();
    // ─── Ariadne's Thread [AT-0178] ─────────────────────
    // What: Probe ScreenCaptureKit getShareableContent when CGPreflight is false
    // Why:  Official capture API is ground truth; CGPreflight stays false for the other signed copy
    // Date: 2026-08-26
    // Related: [AT-0035] MacPermissions.mm:hasScreenRecording, [AT-0176] Application.cpp:ensureScreenRecording
    // ─────────────────────────────────────────────────────
    static bool probeScreenRecording();
    static bool requestScreenRecording();
    static void relaunchApp();
    // ─── Ariadne's Thread [AT-0205] ─────────────────────
    // What: Allow-list for QEvent::Quit so closing a window cannot kill the agent
    // Why:  Tray / Cmd+Q / Sparkle / Screen Recording relaunch must still quit
    // Date: 2026-08-27
    // Related: [AT-0204] Application.cpp:eventFilter, [AT-0172] MacPermissions.mm:relaunchApp
    // ─────────────────────────────────────────────────────
    static void allowQuit(const char *reason);
    static bool shouldAcceptApplicationQuit();
    static QString runningAppPath();
    static bool hasCamera();
    static void requestCamera(const std::function<void(bool)> &done);
};
