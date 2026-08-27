#include "auth/KeychainStore.h"

#include <QDebug>
#include <QSettings>

// ─── Ariadne's Thread [AT-0171] ─────────────────────
// What: Persist Firebase session in QSettings, not macOS Keychain
// Why:  Keychain SecItemCopyMatching prompts after codesign; only Screen Recording TCC is allowed
// Date: 2026-08-26
// Related: [AT-0014] ISecureStore.h, [AT-0018] AuthSession.cpp:persist
// ─────────────────────────────────────────────────────
bool KeychainStore::write(const QString &key, const QByteArray &value, QString *errorCode)
{
    qInfo() << "KeychainStore: write key=" << key << " bytes=" << value.size();
    QSettings settings;
    settings.beginGroup(QStringLiteral("auth"));
    settings.setValue(key, value);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        qWarning() << "KeychainStore: QSettings write failed status=" << static_cast<int>(settings.status())
                   << " key=" << key;
        if (errorCode) {
            *errorCode = QStringLiteral("KEYCHAIN_UNAVAILABLE");
        }
        return false;
    }
    qInfo() << "KeychainStore: write ok key=" << key << " status=" << static_cast<int>(settings.status());
    return true;
}

QByteArray KeychainStore::read(const QString &key, QString *errorCode)
{
    qInfo() << "KeychainStore: read key=" << key;
    QSettings settings;
    settings.beginGroup(QStringLiteral("auth"));
    if (settings.status() != QSettings::NoError) {
        qWarning() << "KeychainStore: QSettings read failed status=" << static_cast<int>(settings.status())
                   << " key=" << key;
        if (errorCode) {
            *errorCode = QStringLiteral("KEYCHAIN_UNAVAILABLE");
        }
        return {};
    }
    const QByteArray value = settings.value(key).toByteArray();
    qInfo() << "KeychainStore: read key=" << key << " bytes=" << value.size() << " empty=" << value.isEmpty();
    return value;
}

bool KeychainStore::remove(const QString &key, QString *errorCode)
{
    qInfo() << "KeychainStore: remove key=" << key;
    QSettings settings;
    settings.beginGroup(QStringLiteral("auth"));
    settings.remove(key);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        qWarning() << "KeychainStore: QSettings remove failed status=" << static_cast<int>(settings.status())
                   << " key=" << key;
        if (errorCode) {
            *errorCode = QStringLiteral("KEYCHAIN_UNAVAILABLE");
        }
        return false;
    }
    qInfo() << "KeychainStore: remove ok key=" << key;
    return true;
}
