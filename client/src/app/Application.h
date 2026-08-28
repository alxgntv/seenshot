#pragma once

#include <QEvent>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class AnnotateWindow;
class AuthSession;
class CloudClient;
class FirstRunWizard;
class ISecureStore;
class MacHotkeyBackend;
class QNetworkAccessManager;
class RegionPicker;
class SettingsWindow;
class TrayController;

// ─── Ariadne's Thread [AT-0024] ─────────────────────
// What: App coordinator for tray, hotkey, capture, settings
// Why:  Single place to start region capture without races
// Date: 2026-08-25
// Related: client/src/app/Application.cpp
// ─────────────────────────────────────────────────────
class Application : public QObject {
    Q_OBJECT
public:
    explicit Application(QObject *parent = nullptr);
    ~Application() override;
    void start();

public slots:
    void beginCapture();
    void beginFullScreenCapture();
    void openSettings();
    void confirmQuit();
    void applyHotkeys();

private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void onRegionPicked(const QRect &rect);
    void captureRect(const QRect &rect, const QString &kind);
    void showAnnotate(const QImage &image);
    void restoreEditorIfNeeded();
    void setCaptureFlag(bool capturing);
    bool ensureScreenRecording();
    void showScreenRecordingHelp();
    void teardownNativeWindows();
    void handleOpenUrl(const QUrl &url);

    QNetworkAccessManager *m_nam = nullptr;
    ISecureStore *m_store = nullptr;
    AuthSession *m_auth = nullptr;
    CloudClient *m_cloud = nullptr;
    MacHotkeyBackend *m_hotkeys = nullptr;
    TrayController *m_tray = nullptr;
    QPointer<RegionPicker> m_picker;
    QPointer<AnnotateWindow> m_editor;
    QPointer<SettingsWindow> m_settings;
    QPointer<FirstRunWizard> m_onboarding;
    bool m_capturing = false;
    bool m_requestedScreenRecording = false;
    bool m_ensuringScreenRecording = false;
};
