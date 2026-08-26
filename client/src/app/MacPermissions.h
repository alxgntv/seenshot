#pragma once

#include <functional>

class QWidget;

class MacPermissions {
public:
    static void activateApp();
    static void pinCaptureOverlay(QWidget *overlay);
    static void pinFloatingToolWindow(QWidget *overlay);
    static void openScreenRecordingSettings();
    static void openCameraSettings();
    static bool hasScreenRecording();
    static bool requestScreenRecording();
    static bool hasCamera();
    static void requestCamera(const std::function<void(bool)> &done);
};
