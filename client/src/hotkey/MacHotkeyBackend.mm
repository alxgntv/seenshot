#include "hotkey/MacHotkeyBackend.h"

#include <QDebug>
#include <QHash>

#import <Carbon/Carbon.h>

namespace {

MacHotkeyBackend *gBackend = nullptr;

constexpr UInt32 kPathHotKeyId = 1;
constexpr UInt32 kFullHotKeyId = 2;

OSStatus hotKeyHandler(EventHandlerCallRef, EventRef event, void *)
{
    EventHotKeyID hotKeyId{};
    const OSStatus err = GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr,
                                           sizeof(hotKeyId), nullptr, &hotKeyId);
    if (err != noErr || !gBackend) {
        qWarning() << "MacHotkeyBackend: handler id read failed" << err;
        return err == noErr ? noErr : err;
    }
    qInfo() << "MacHotkeyBackend: hotkey fired id=" << hotKeyId.id;
    if (hotKeyId.id == kPathHotKeyId) {
        emit gBackend->pathCaptureTriggered();
    } else if (hotKeyId.id == kFullHotKeyId) {
        emit gBackend->fullScreenCaptureTriggered();
    }
    return noErr;
}

bool parseSpec(const QString &spec, UInt32 *keyCode, UInt32 *modifiers)
{
    *keyCode = 0;
    *modifiers = 0;
    const QString normalized = spec.toLower();
    qInfo() << "MacHotkeyBackend: parse" << normalized;
    if (normalized.contains(QLatin1String("cmd")) || normalized.contains(QLatin1String("command"))
        || normalized.contains(QLatin1String("meta"))) {
        *modifiers |= cmdKey;
    }
    if (normalized.contains(QLatin1String("shift"))) {
        *modifiers |= shiftKey;
    }
    if (normalized.contains(QLatin1String("alt")) || normalized.contains(QLatin1String("option"))) {
        *modifiers |= optionKey;
    }
    if (normalized.contains(QLatin1String("ctrl")) || normalized.contains(QLatin1String("control"))) {
        *modifiers |= controlKey;
    }
    const QString key = normalized.section(QLatin1Char('+'), -1).trimmed();
    static const QHash<QString, UInt32> kCodes = {
        {QStringLiteral("0"), kVK_ANSI_0}, {QStringLiteral("1"), kVK_ANSI_1},
        {QStringLiteral("2"), kVK_ANSI_2}, {QStringLiteral("3"), kVK_ANSI_3},
        {QStringLiteral("4"), kVK_ANSI_4}, {QStringLiteral("5"), kVK_ANSI_5},
        {QStringLiteral("6"), kVK_ANSI_6}, {QStringLiteral("7"), kVK_ANSI_7},
        {QStringLiteral("8"), kVK_ANSI_8}, {QStringLiteral("9"), kVK_ANSI_9},
        {QStringLiteral("a"), kVK_ANSI_A}, {QStringLiteral("b"), kVK_ANSI_B},
        {QStringLiteral("c"), kVK_ANSI_C}, {QStringLiteral("d"), kVK_ANSI_D},
        {QStringLiteral("e"), kVK_ANSI_E}, {QStringLiteral("f"), kVK_ANSI_F},
        {QStringLiteral("g"), kVK_ANSI_G}, {QStringLiteral("h"), kVK_ANSI_H},
        {QStringLiteral("i"), kVK_ANSI_I}, {QStringLiteral("j"), kVK_ANSI_J},
        {QStringLiteral("k"), kVK_ANSI_K}, {QStringLiteral("l"), kVK_ANSI_L},
        {QStringLiteral("m"), kVK_ANSI_M}, {QStringLiteral("n"), kVK_ANSI_N},
        {QStringLiteral("o"), kVK_ANSI_O}, {QStringLiteral("p"), kVK_ANSI_P},
        {QStringLiteral("q"), kVK_ANSI_Q}, {QStringLiteral("r"), kVK_ANSI_R},
        {QStringLiteral("s"), kVK_ANSI_S}, {QStringLiteral("t"), kVK_ANSI_T},
        {QStringLiteral("u"), kVK_ANSI_U}, {QStringLiteral("v"), kVK_ANSI_V},
        {QStringLiteral("w"), kVK_ANSI_W}, {QStringLiteral("x"), kVK_ANSI_X},
        {QStringLiteral("y"), kVK_ANSI_Y}, {QStringLiteral("z"), kVK_ANSI_Z},
    };
    if (!kCodes.contains(key)) {
        qWarning() << "MacHotkeyBackend: unsupported key" << key;
        return false;
    }
    if (*modifiers == 0) {
        qWarning() << "MacHotkeyBackend: modifiers required";
        return false;
    }
    *keyCode = kCodes.value(key);
    qInfo() << "MacHotkeyBackend: parsed keyCode=" << *keyCode << " modifiers=" << *modifiers;
    return true;
}

bool registerOne(const QString &spec, UInt32 id, EventHotKeyRef *outRef, QString *errorCode)
{
    UInt32 keyCode = 0;
    UInt32 modifiers = 0;
    if (!parseSpec(spec, &keyCode, &modifiers)) {
        if (errorCode) {
            *errorCode = QStringLiteral("HOTKEY_IN_USE");
        }
        return false;
    }
    EventHotKeyRef ref = nullptr;
    const EventHotKeyID keyId{'SNsh', id};
    const OSStatus status = RegisterEventHotKey(keyCode, modifiers, keyId, GetApplicationEventTarget(), 0, &ref);
    if (status != noErr) {
        qWarning() << "MacHotkeyBackend: RegisterEventHotKey failed spec=" << spec << " id=" << id
                   << " status=" << status;
        if (errorCode) {
            *errorCode = QStringLiteral("HOTKEY_IN_USE");
        }
        return false;
    }
    *outRef = ref;
    qInfo() << "MacHotkeyBackend: registered" << spec << " id=" << id << " keyCode=" << keyCode;
    return true;
}

} // namespace

MacHotkeyBackend::MacHotkeyBackend(QObject *parent)
    : QObject(parent)
{
    gBackend = this;
    EventTypeSpec spec;
    spec.eventClass = kEventClassKeyboard;
    spec.eventKind = kEventHotKeyPressed;
    InstallApplicationEventHandler(hotKeyHandler, 1, &spec, nullptr, nullptr);
    qInfo() << "MacHotkeyBackend: event handler installed";
}

MacHotkeyBackend::~MacHotkeyBackend()
{
    unregisterAll();
    if (gBackend == this) {
        gBackend = nullptr;
    }
}

void MacHotkeyBackend::unregisterAll()
{
    if (m_pathHotKeyRef) {
        UnregisterEventHotKey(static_cast<EventHotKeyRef>(m_pathHotKeyRef));
        m_pathHotKeyRef = nullptr;
        qInfo() << "MacHotkeyBackend: unregistered path";
    }
    if (m_fullHotKeyRef) {
        UnregisterEventHotKey(static_cast<EventHotKeyRef>(m_fullHotKeyRef));
        m_fullHotKeyRef = nullptr;
        qInfo() << "MacHotkeyBackend: unregistered full screen";
    }
}

// ─── Ariadne's Thread [AT-0070] ─────────────────────
// What: Register path and full-screen global hotkeys with separate Carbon IDs
// Why:  Menu needs both shots; one RegisterEventHotKey could not tell them apart
// Date: 2026-08-25
// Related: [AT-0023] MacHotkeyBackend.mm, [AT-0021] LocalStore.cpp
// ─────────────────────────────────────────────────────
bool MacHotkeyBackend::registerHotkeys(const QString &pathSpec, const QString &fullScreenSpec, QString *errorCode)
{
    unregisterAll();
    qInfo() << "MacHotkeyBackend: registerHotkeys path=" << pathSpec << " full=" << fullScreenSpec;
    if (pathSpec == fullScreenSpec) {
        qWarning() << "MacHotkeyBackend: path and full screen specs collide";
        if (errorCode) {
            *errorCode = QStringLiteral("HOTKEY_IN_USE");
        }
        return false;
    }
    EventHotKeyRef pathRef = nullptr;
    EventHotKeyRef fullRef = nullptr;
    const bool pathOk = registerOne(pathSpec, kPathHotKeyId, &pathRef, errorCode);
    if (pathOk) {
        m_pathHotKeyRef = pathRef;
    }
    QString fullError;
    const bool fullOk = registerOne(fullScreenSpec, kFullHotKeyId, &fullRef, &fullError);
    if (fullOk) {
        m_fullHotKeyRef = fullRef;
    } else if (errorCode && errorCode->isEmpty()) {
        *errorCode = fullError;
    }
    return pathOk && fullOk;
}
