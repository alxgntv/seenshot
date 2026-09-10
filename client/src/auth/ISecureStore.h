#pragma once

#include <QByteArray>
#include <QString>

// ─── Ariadne's Thread [AT-0644] ─────────────────────
// What: Secure store interface for Firebase refresh token
// Why:  macOS Keychain is the store, not the preferences plist
// Date: 2026-09-09
// Related: [AT-0644] client/src/auth/KeychainStore.cpp
// ─────────────────────────────────────────────────────
class ISecureStore {
public:
    virtual ~ISecureStore() = default;
    virtual bool write(const QString &key, const QByteArray &value, QString *errorCode) = 0;
    virtual QByteArray read(const QString &key, QString *errorCode) = 0;
    virtual bool remove(const QString &key, QString *errorCode) = 0;
};
