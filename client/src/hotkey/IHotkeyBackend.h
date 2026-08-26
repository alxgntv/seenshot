#pragma once

#include <QString>

class IHotkeyBackend {
public:
    virtual ~IHotkeyBackend() = default;
    virtual bool registerHotkeys(const QString &pathSpec, const QString &fullScreenSpec, QString *errorCode) = 0;
};
