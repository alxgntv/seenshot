#include "auth/KeychainStore.h"

#include <QDebug>

#import <Foundation/Foundation.h>
#import <Security/Security.h>

namespace {

NSString *serviceName()
{
    return @"com.seenshot.auth";
}

} // namespace

// ─── Ariadne's Thread [AT-0015] ─────────────────────
// What: Store refresh token in macOS Keychain
// Why:  Session must persist; never write tokens as plaintext
// Date: 2026-08-25
// Related: [AT-0014] ISecureStore.h
// ─────────────────────────────────────────────────────
bool KeychainStore::write(const QString &key, const QByteArray &value, QString *errorCode)
{
    qInfo() << "KeychainStore: write key=" << key << " bytes=" << value.size();
    remove(key, nullptr);
    NSString *account = key.toNSString();
    NSData *data = [NSData dataWithBytes:value.constData() length:static_cast<NSUInteger>(value.size())];
    NSDictionary *query = @{
        (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService : serviceName(),
        (__bridge id)kSecAttrAccount : account,
        (__bridge id)kSecValueData : data,
        (__bridge id)kSecAttrAccessible : (__bridge id)kSecAttrAccessibleAfterFirstUnlock,
    };
    const OSStatus status = SecItemAdd((__bridge CFDictionaryRef)query, nullptr);
    if (status != errSecSuccess) {
        qWarning() << "KeychainStore: SecItemAdd failed" << status;
        if (errorCode) {
            *errorCode = QStringLiteral("KEYCHAIN_UNAVAILABLE");
        }
        return false;
    }
    return true;
}

QByteArray KeychainStore::read(const QString &key, QString *errorCode)
{
    qInfo() << "KeychainStore: read key=" << key;
    NSDictionary *query = @{
        (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService : serviceName(),
        (__bridge id)kSecAttrAccount : key.toNSString(),
        (__bridge id)kSecReturnData : @YES,
        (__bridge id)kSecMatchLimit : (__bridge id)kSecMatchLimitOne,
    };
    CFTypeRef result = nullptr;
    const OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &result);
    if (status == errSecItemNotFound) {
        qInfo() << "KeychainStore: not found";
        return {};
    }
    if (status != errSecSuccess || !result) {
        qWarning() << "KeychainStore: SecItemCopyMatching failed" << status;
        if (errorCode) {
            *errorCode = QStringLiteral("KEYCHAIN_UNAVAILABLE");
        }
        return {};
    }
    NSData *data = (__bridge NSData *)result;
    const QByteArray bytes(static_cast<const char *>(data.bytes), static_cast<int>(data.length));
    CFRelease(result);
    return bytes;
}

bool KeychainStore::remove(const QString &key, QString *errorCode)
{
    qInfo() << "KeychainStore: remove key=" << key;
    NSDictionary *query = @{
        (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService : serviceName(),
        (__bridge id)kSecAttrAccount : key.toNSString(),
    };
    const OSStatus status = SecItemDelete((__bridge CFDictionaryRef)query);
    if (status != errSecSuccess && status != errSecItemNotFound) {
        qWarning() << "KeychainStore: SecItemDelete failed" << status;
        if (errorCode) {
            *errorCode = QStringLiteral("KEYCHAIN_UNAVAILABLE");
        }
        return false;
    }
    return true;
}
