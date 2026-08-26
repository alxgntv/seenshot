#pragma once

#include "hotkey/IHotkeyBackend.h"

#include <QObject>

class MacHotkeyBackend : public QObject, public IHotkeyBackend {
    Q_OBJECT
public:
    explicit MacHotkeyBackend(QObject *parent = nullptr);
    ~MacHotkeyBackend() override;
    bool registerHotkeys(const QString &pathSpec, const QString &fullScreenSpec, QString *errorCode) override;

signals:
    void pathCaptureTriggered();
    void fullScreenCaptureTriggered();

private:
    void unregisterAll();

    void *m_pathHotKeyRef = nullptr;
    void *m_fullHotKeyRef = nullptr;
};
