#pragma once

#include <QObject>
#include <QString>

class QAction;
class QMenu;
class QSystemTrayIcon;

class TrayController : public QObject {
    Q_OBJECT
public:
    explicit TrayController(QObject *parent = nullptr);
    void show();
    void refreshCaptureShortcuts(const QString &fullScreenSpec, const QString &pathSpec);

signals:
    void fullScreenCaptureRequested();
    void captureRequested();
    void settingsRequested();
    void quitRequested();

private:
    QSystemTrayIcon *m_tray = nullptr;
    QAction *m_fullScreenAction = nullptr;
    QAction *m_pathAction = nullptr;
};
