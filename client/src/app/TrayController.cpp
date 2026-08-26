#include "app/TrayController.h"

#include "app/MacIcons.h"
#include "local/LocalStore.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>

// ─── Ariadne's Thread [AT-0070] ─────────────────────
// What: Tray actions for Full Screen Shot and Path Screen Shot with native shortcut text
// Why:  Menu must show the live hotkeys; Capture was one unlabeled item
// Date: 2026-08-25
// Related: [AT-0065] TrayController.cpp, [AT-0021] LocalStore.cpp
// ─────────────────────────────────────────────────────
TrayController::TrayController(QObject *parent)
    : QObject(parent)
{
    const QString icns = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../Resources/SeenShot.icns"));
    QIcon appIcon(icns);
    if (appIcon.isNull()) {
        appIcon = macToolbarIcon(QStringLiteral("eye"));
        qWarning() << "TrayController: bundle icns missing, fallback eye icns=" << icns;
    }
    qInfo() << "TrayController: app icon null=" << appIcon.isNull() << "icns=" << icns;
    m_tray = new QSystemTrayIcon(appIcon, this);
    auto *menu = new QMenu();
    m_fullScreenAction = menu->addAction(QStringLiteral("Full Screen Shot"), this,
                                         &TrayController::fullScreenCaptureRequested);
    m_pathAction = menu->addAction(QStringLiteral("Path Screen Shot"), this,
                                   &TrayController::captureRequested);
    m_fullScreenAction->setShortcutVisibleInContextMenu(true);
    m_pathAction->setShortcutVisibleInContextMenu(true);
    m_fullScreenAction->setShortcutContext(Qt::WidgetShortcut);
    m_pathAction->setShortcutContext(Qt::WidgetShortcut);
    menu->addAction(QStringLiteral("Settings"), this, &TrayController::settingsRequested);
    menu->addSeparator();
    menu->addAction(QStringLiteral("Quit"), this, &TrayController::quitRequested);
    m_tray->setContextMenu(menu);
    m_tray->setToolTip(QStringLiteral("SeenShot"));
    connect(m_tray, &QSystemTrayIcon::activated, this, [](QSystemTrayIcon::ActivationReason reason) {
        qInfo() << "TrayController: activated reason=" << static_cast<int>(reason)
                << " capture only from menu or hotkey";
    });
    refreshCaptureShortcuts(LocalStore::fullScreenHotkeySpec(), LocalStore::hotkeySpec());
    qInfo() << "TrayController: created";
}

void TrayController::show()
{
    m_tray->show();
    qInfo() << "TrayController: shown available=" << QSystemTrayIcon::isSystemTrayAvailable();
}

void TrayController::refreshCaptureShortcuts(const QString &fullScreenSpec, const QString &pathSpec)
{
    if (m_fullScreenAction) {
        const QKeySequence seq = LocalStore::keySequenceFromSpec(fullScreenSpec);
        m_fullScreenAction->setShortcut(seq);
        qInfo() << "TrayController: Full Screen Shot shortcut=" << LocalStore::nativeHotkeyLabel(fullScreenSpec);
    }
    if (m_pathAction) {
        const QKeySequence seq = LocalStore::keySequenceFromSpec(pathSpec);
        m_pathAction->setShortcut(seq);
        qInfo() << "TrayController: Path Screen Shot shortcut=" << LocalStore::nativeHotkeyLabel(pathSpec);
    }
}
