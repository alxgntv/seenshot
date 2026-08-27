#pragma once

#include <QByteArray>
#include <QString>

// ─── Ariadne's Thread [AT-0014] ─────────────────────
// What: Secure store interface for later Linux/Windows ports
// Why:  Session file is QSettings; no macOS Keychain prompts
// Date: 2026-08-25
// Related: client/src/auth/KeychainStore.h
// ─────────────────────────────────────────────────────
class ISecureStore {
public:
    virtual ~ISecureStore() = default;
    virtual bool write(const QString &key, const QByteArray &value, QString *errorCode) = 0;
    virtual QByteArray read(const QString &key, QString *errorCode) = 0;
    virtual bool remove(const QString &key, QString *errorCode) = 0;
};
