#include "auth/KeychainStore.h"

#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

namespace {

constexpr const char *kService = "com.seenshot.app";

QMutex &storeMutex()
{
    static QMutex mutex;
    return mutex;
}

QString osStatusText(OSStatus status)
{
    CFStringRef msg = SecCopyErrorMessageString(status, nullptr);
    if (msg == nullptr) {
        return QString::number(static_cast<int>(status));
    }
    const CFIndex len = CFStringGetLength(msg);
    const CFIndex max = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
    QByteArray buf(static_cast<int>(max), '\0');
    CFStringGetCString(msg, buf.data(), max, kCFStringEncodingUTF8);
    CFRelease(msg);
    return QString::fromUtf8(buf.constData());
}

void setUnavailable(QString *errorCode)
{
    if (errorCode) {
        *errorCode = QStringLiteral("KEYCHAIN_UNAVAILABLE");
    }
}

CFStringRef cfStringFromUtf8(const QByteArray &utf8)
{
    return CFStringCreateWithCString(kCFAllocatorDefault, utf8.constData(), kCFStringEncodingUTF8);
}

// ─── Ariadne's Thread [AT-0644] ─────────────────────
// What: Build a SecItem query for the file-based macOS Keychain
// Why:  Data protection Keychain needs keychain-access-groups plus a profile. Developer ID has neither. TN3137 file-based Keychain does not
// Date: 2026-09-09
// Related: [AT-0644] KeychainStore.cpp:writeUnlocked, Apple TN3137, Apple SecItemAdd
// ─────────────────────────────────────────────────────
CFMutableDictionaryRef baseQuery(const QString &account)
{
    CFMutableDictionaryRef query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                                             &kCFTypeDictionaryValueCallBacks);
    if (query == nullptr) {
        qWarning() << "KeychainStore: CFDictionaryCreateMutable failed account=" << account;
        return nullptr;
    }
    const QByteArray accountUtf8 = account.toUtf8();
    CFStringRef service = CFStringCreateWithCString(kCFAllocatorDefault, kService, kCFStringEncodingUTF8);
    CFStringRef acc = cfStringFromUtf8(accountUtf8);
    if (service == nullptr || acc == nullptr) {
        qWarning() << "KeychainStore: CFStringCreate failed account=" << account;
        if (service) {
            CFRelease(service);
        }
        if (acc) {
            CFRelease(acc);
        }
        CFRelease(query);
        return nullptr;
    }
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, service);
    CFDictionarySetValue(query, kSecAttrAccount, acc);
    CFRelease(service);
    CFRelease(acc);
    return query;
}

QByteArray readLegacySettings(const QString &key)
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("auth"));
    const QByteArray value = settings.value(key).toByteArray();
    qInfo() << "KeychainStore: legacy QSettings read key=" << key << " bytes=" << value.size()
            << " empty=" << value.isEmpty();
    return value;
}

void removeLegacySettings(const QString &key)
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("auth"));
    if (!settings.contains(key)) {
        return;
    }
    settings.remove(key);
    settings.sync();
    qInfo() << "KeychainStore: legacy QSettings removed key=" << key
            << " status=" << static_cast<int>(settings.status());
}

bool writeUnlocked(const QString &key, const QByteArray &value, QString *errorCode)
{
    qInfo() << "KeychainStore: write key=" << key << " bytes=" << value.size();
    CFMutableDictionaryRef query = baseQuery(key);
    if (query == nullptr) {
        setUnavailable(errorCode);
        return false;
    }
    CFDataRef data = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(value.constData()),
                                  static_cast<CFIndex>(value.size()));
    if (data == nullptr) {
        qWarning() << "KeychainStore: CFDataCreate failed key=" << key << " bytes=" << value.size();
        CFRelease(query);
        setUnavailable(errorCode);
        return false;
    }
    CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                                             &kCFTypeDictionaryValueCallBacks);
    if (attrs == nullptr) {
        qWarning() << "KeychainStore: update attrs alloc failed key=" << key;
        CFRelease(data);
        CFRelease(query);
        setUnavailable(errorCode);
        return false;
    }
    CFDictionarySetValue(attrs, kSecValueData, data);
    OSStatus status = SecItemUpdate(query, attrs);
    qInfo() << "KeychainStore: SecItemUpdate key=" << key << " status=" << static_cast<int>(status)
            << " text=" << osStatusText(status);
    if (status == errSecItemNotFound) {
        CFDictionarySetValue(query, kSecValueData, data);
        CFStringRef label = CFStringCreateWithCString(kCFAllocatorDefault, "SeenShot", kCFStringEncodingUTF8);
        if (label) {
            CFDictionarySetValue(query, kSecAttrLabel, label);
            CFRelease(label);
        }
        status = SecItemAdd(query, nullptr);
        qInfo() << "KeychainStore: SecItemAdd key=" << key << " status=" << static_cast<int>(status)
                << " text=" << osStatusText(status);
        if (status == errSecDuplicateItem) {
            CFDictionaryRemoveValue(query, kSecValueData);
            CFDictionaryRemoveValue(query, kSecAttrLabel);
            status = SecItemUpdate(query, attrs);
            qInfo() << "KeychainStore: SecItemUpdate after duplicate key=" << key
                    << " status=" << static_cast<int>(status) << " text=" << osStatusText(status);
        }
    }
    CFRelease(attrs);
    CFRelease(data);
    CFRelease(query);
    if (status != errSecSuccess) {
        qWarning() << "KeychainStore: write failed key=" << key << " status=" << static_cast<int>(status)
                   << " text=" << osStatusText(status);
        setUnavailable(errorCode);
        return false;
    }
    removeLegacySettings(key);
    qInfo() << "KeychainStore: write ok key=" << key << " bytes=" << value.size();
    return true;
}

QByteArray readKeychainUnlocked(const QString &key, QString *errorCode, bool *unavailable)
{
    *unavailable = false;
    CFMutableDictionaryRef query = baseQuery(key);
    if (query == nullptr) {
        *unavailable = true;
        setUnavailable(errorCode);
        return {};
    }
    CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef result = nullptr;
    OSStatus status = SecItemCopyMatching(query, &result);
    CFRelease(query);
    qInfo() << "KeychainStore: SecItemCopyMatching key=" << key << " status=" << static_cast<int>(status)
            << " text=" << osStatusText(status);
    if (status == errSecSuccess && result != nullptr && CFGetTypeID(result) == CFDataGetTypeID()) {
        CFDataRef data = static_cast<CFDataRef>(result);
        const char *ptr = reinterpret_cast<const char *>(CFDataGetBytePtr(data));
        const int len = static_cast<int>(CFDataGetLength(data));
        const QByteArray value(ptr ? ptr : "", len);
        CFRelease(result);
        removeLegacySettings(key);
        qInfo() << "KeychainStore: read ok key=" << key << " bytes=" << value.size();
        return value;
    }
    if (result) {
        CFRelease(result);
    }
    if (status != errSecItemNotFound && status != errSecSuccess) {
        qWarning() << "KeychainStore: read failed key=" << key << " status=" << static_cast<int>(status)
                   << " text=" << osStatusText(status);
        *unavailable = true;
        setUnavailable(errorCode);
        return {};
    }
    return {};
}

} // namespace

// ─── Ariadne's Thread [AT-0644] ─────────────────────
// What: Persist Firebase session in the macOS file-based Keychain
// Why:  Refresh token must not live in the preferences plist
// Date: 2026-09-09
// Related: [AT-0018] AuthSession.cpp:persist, Apple SecItemAdd, Apple TN3137
// ─────────────────────────────────────────────────────
bool KeychainStore::write(const QString &key, const QByteArray &value, QString *errorCode)
{
    QMutexLocker lock(&storeMutex());
    return writeUnlocked(key, value, errorCode);
}

QByteArray KeychainStore::read(const QString &key, QString *errorCode)
{
    QMutexLocker lock(&storeMutex());
    qInfo() << "KeychainStore: read key=" << key;
    bool unavailable = false;
    const QByteArray stored = readKeychainUnlocked(key, errorCode, &unavailable);
    if (unavailable) {
        return {};
    }
    if (!stored.isEmpty()) {
        return stored;
    }
    const QByteArray legacy = readLegacySettings(key);
    if (legacy.isEmpty()) {
        qInfo() << "KeychainStore: read empty key=" << key;
        return {};
    }
    QString migrateError;
    const bool migrated = writeUnlocked(key, legacy, &migrateError);
    qInfo() << "KeychainStore: migrate legacy key=" << key << " bytes=" << legacy.size()
            << " ok=" << migrated << " error=" << migrateError;
    if (!migrated) {
        qWarning() << "KeychainStore: keep legacy QSettings key=" << key << " until Keychain write succeeds";
    }
    return legacy;
}

bool KeychainStore::remove(const QString &key, QString *errorCode)
{
    QMutexLocker lock(&storeMutex());
    qInfo() << "KeychainStore: remove key=" << key;
    CFMutableDictionaryRef query = baseQuery(key);
    if (query == nullptr) {
        setUnavailable(errorCode);
        return false;
    }
    OSStatus status = SecItemDelete(query);
    CFRelease(query);
    removeLegacySettings(key);
    qInfo() << "KeychainStore: SecItemDelete key=" << key << " status=" << static_cast<int>(status)
            << " text=" << osStatusText(status);
    if (status != errSecSuccess && status != errSecItemNotFound) {
        qWarning() << "KeychainStore: remove failed key=" << key << " status=" << static_cast<int>(status)
                   << " text=" << osStatusText(status);
        setUnavailable(errorCode);
        return false;
    }
    qInfo() << "KeychainStore: remove ok key=" << key << " missing=" << (status == errSecItemNotFound);
    return true;
}
