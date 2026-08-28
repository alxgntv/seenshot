#include "local/LocalStore.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QKeyCombination>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

namespace {

QString editorSessionDir()
{
    return LocalStore::settingsPath() + QStringLiteral("/editor-session");
}

void removeEditorSessionDir(const QString &path)
{
    if (path.isEmpty() || !QDir(path).exists()) {
        return;
    }
    if (!QDir(path).removeRecursively()) {
        qWarning() << "LocalStore: could not remove session dir" << path;
    }
}

} // namespace

QString LocalStore::settingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}

QString LocalStore::tempCapturePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/seenshot");
    QDir().mkpath(dir);
    const QString path = dir + QLatin1Char('/') + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".png");
    qInfo() << "LocalStore: temp capture path" << path;
    return path;
}

void LocalStore::removeTempCapture(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    if (QFile::remove(path)) {
        qInfo() << "LocalStore: removed temp" << path;
    } else {
        qWarning() << "LocalStore: could not remove temp" << path;
    }
}

bool LocalStore::firstRunCompleted()
{
    QSettings settings;
    return settings.value(QStringLiteral("firstRunCompleted"), false).toBool();
}

void LocalStore::setFirstRunCompleted()
{
    QSettings settings;
    settings.setValue(QStringLiteral("firstRunCompleted"), true);
    qInfo() << "LocalStore: first run marked complete";
}

// ─── Ariadne's Thread [AT-0301] ─────────────────────
// What: Persist onboardingVersion=1 when the 6-step QWizard finishes
// Why:  firstRunCompleted is already true on 0.1.4 installs; that must not skip setup
// Date: 2026-08-28
// Related: [AT-0303] Application.cpp:start, [AT-0304] main.cpp
// ─────────────────────────────────────────────────────
int LocalStore::onboardingVersion()
{
    QSettings settings;
    const int version = settings.value(QStringLiteral("onboardingVersion"), 0).toInt();
    qInfo() << "LocalStore: onboardingVersion=" << version;
    return version;
}

bool LocalStore::onboardingCompleted()
{
    const int version = onboardingVersion();
    const bool done = version >= 1;
    qInfo() << "LocalStore: onboardingCompleted=" << done;
    return done;
}

void LocalStore::setOnboardingCompleted()
{
    QSettings settings;
    settings.setValue(QStringLiteral("onboardingVersion"), 1);
    settings.setValue(QStringLiteral("firstRunCompleted"), true);
    settings.sync();
    qInfo() << "LocalStore: onboardingVersion=1 firstRunCompleted=true";
}

void LocalStore::resetOnboarding()
{
    QSettings settings;
    settings.setValue(QStringLiteral("onboardingVersion"), 0);
    settings.sync();
    qInfo() << "LocalStore: onboardingVersion reset to 0";
}

// ─── Ariadne's Thread [AT-0175] ─────────────────────
// What: Persist that this Mac already registered SeenShot with Screen Recording TCC
// Why:  CGRequestScreenCaptureAccess every launch showed Open Settings while another signed copy was already on
// Date: 2026-08-26
// Related: [AT-0176] Application.cpp:ensureScreenRecording, [AT-0178] MacPermissions.mm:probeScreenRecording
// ─────────────────────────────────────────────────────
bool LocalStore::screenRecordingRegistered()
{
    QSettings settings;
    const bool ok = settings.value(QStringLiteral("screenRecordingRegistered"), false).toBool();
    qInfo() << "LocalStore: screenRecordingRegistered=" << ok;
    return ok;
}

void LocalStore::setScreenRecordingRegistered()
{
    QSettings settings;
    if (settings.value(QStringLiteral("screenRecordingRegistered"), false).toBool()) {
        qInfo() << "LocalStore: screenRecordingRegistered already true";
        return;
    }
    settings.setValue(QStringLiteral("screenRecordingRegistered"), true);
    qInfo() << "LocalStore: screenRecordingRegistered set";
}

QString LocalStore::hotkeySpec()
{
    QSettings settings;
    return settings.value(QStringLiteral("hotkeySpec"), QStringLiteral("cmd+shift+2")).toString();
}

void LocalStore::setHotkeySpec(const QString &spec)
{
    QSettings settings;
    settings.setValue(QStringLiteral("hotkeySpec"), spec);
    qInfo() << "LocalStore: path hotkey" << spec;
}

QString LocalStore::fullScreenHotkeySpec()
{
    QSettings settings;
    return settings.value(QStringLiteral("fullScreenHotkeySpec"), QStringLiteral("cmd+shift+6")).toString();
}

void LocalStore::setFullScreenHotkeySpec(const QString &spec)
{
    QSettings settings;
    settings.setValue(QStringLiteral("fullScreenHotkeySpec"), spec);
    qInfo() << "LocalStore: full screen hotkey" << spec;
}

QKeySequence LocalStore::keySequenceFromSpec(const QString &spec)
{
    QStringList parts;
    const QStringList tokens = spec.toLower().split(QLatin1Char('+'), Qt::SkipEmptyParts);
    for (const QString &token : tokens) {
        if (token == QLatin1String("cmd") || token == QLatin1String("command") || token == QLatin1String("meta")) {
            parts.append(QStringLiteral("Meta"));
        } else if (token == QLatin1String("shift")) {
            parts.append(QStringLiteral("Shift"));
        } else if (token == QLatin1String("alt") || token == QLatin1String("option")) {
            parts.append(QStringLiteral("Alt"));
        } else if (token == QLatin1String("ctrl") || token == QLatin1String("control")) {
            parts.append(QStringLiteral("Ctrl"));
        } else {
            parts.append(token.toUpper());
        }
    }
    const QKeySequence seq = QKeySequence::fromString(parts.join(QLatin1Char('+')), QKeySequence::PortableText);
    qInfo() << "LocalStore: spec" << spec << "-> sequence" << seq.toString(QKeySequence::PortableText);
    return seq;
}

QString LocalStore::specFromKeySequence(const QKeySequence &sequence)
{
    if (sequence.isEmpty()) {
        qWarning() << "LocalStore: empty key sequence";
        return {};
    }
    const QKeyCombination combo = sequence[0];
    QStringList parts;
    if (combo.keyboardModifiers() & Qt::MetaModifier) {
        parts.append(QStringLiteral("cmd"));
    }
    if (combo.keyboardModifiers() & Qt::ControlModifier) {
        parts.append(QStringLiteral("ctrl"));
    }
    if (combo.keyboardModifiers() & Qt::AltModifier) {
        parts.append(QStringLiteral("alt"));
    }
    if (combo.keyboardModifiers() & Qt::ShiftModifier) {
        parts.append(QStringLiteral("shift"));
    }
    const QString key = QKeySequence(combo.key()).toString(QKeySequence::PortableText).toLower();
    if (key.isEmpty()) {
        qWarning() << "LocalStore: sequence has no key";
        return {};
    }
    parts.append(key);
    const QString spec = parts.join(QLatin1Char('+'));
    qInfo() << "LocalStore: sequence" << sequence.toString(QKeySequence::PortableText) << "-> spec" << spec;
    return spec;
}

QString LocalStore::nativeHotkeyLabel(const QString &spec)
{
    const QString label = keySequenceFromSpec(spec).toString(QKeySequence::NativeText);
    qInfo() << "LocalStore: native label spec=" << spec << "label=" << label;
    return label;
}

QString LocalStore::pendingSignInEmail()
{
    QSettings settings;
    const QString email = settings.value(QStringLiteral("pendingSignInEmail")).toString();
    qInfo() << "LocalStore: pendingSignInEmail chars=" << email.size();
    return email;
}

void LocalStore::setPendingSignInEmail(const QString &email)
{
    QSettings settings;
    settings.setValue(QStringLiteral("pendingSignInEmail"), email);
    qInfo() << "LocalStore: pendingSignInEmail set chars=" << email.size();
}

void LocalStore::clearPendingSignInEmail()
{
    QSettings settings;
    settings.remove(QStringLiteral("pendingSignInEmail"));
    qInfo() << "LocalStore: pendingSignInEmail cleared";
}

// ─── Ariadne's Thread [AT-0091] ─────────────────────
// What: Persist open annotate session for Sparkle relaunch
// Why:  PRD-05 — install must not wipe the current shot and objects
// Date: 2026-08-25
// Related: [AT-0021] LocalStore.h, [AT-0093] AnnotateWindow.cpp:persistSession
// ─────────────────────────────────────────────────────
bool LocalStore::hasEditorSession()
{
    QSettings settings;
    const bool flagged = settings.value(QStringLiteral("editorSession"), false).toBool();
    const QString shot = editorSessionDir() + QStringLiteral("/shot.png");
    const bool ok = flagged && QFile::exists(shot);
    qInfo() << "LocalStore: hasEditorSession=" << ok << " flagged=" << flagged;
    return ok;
}

bool LocalStore::writeEditorSession(const QJsonObject &json, const QImage &shot,
                                    const QHash<QString, QImage> &assets, QString *errorCode)
{
    if (shot.isNull()) {
        qWarning() << "LocalStore: writeEditorSession empty shot";
        if (errorCode) {
            *errorCode = QStringLiteral("UPDATE_PERSIST_FAILED");
        }
        return false;
    }
    const QString finalDir = editorSessionDir();
    const QString tmpDir = settingsPath() + QStringLiteral("/editor-session-writing");
    removeEditorSessionDir(tmpDir);
    if (!QDir().mkpath(tmpDir)) {
        qWarning() << "LocalStore: writeEditorSession mkpath failed" << tmpDir;
        if (errorCode) {
            *errorCode = QStringLiteral("UPDATE_PERSIST_FAILED");
        }
        return false;
    }
    const QString shotPath = tmpDir + QStringLiteral("/shot.png");
    if (!shot.save(shotPath, "PNG")) {
        qWarning() << "LocalStore: writeEditorSession shot save failed";
        removeEditorSessionDir(tmpDir);
        if (errorCode) {
            *errorCode = QStringLiteral("UPDATE_PERSIST_FAILED");
        }
        return false;
    }
    for (auto it = assets.constBegin(); it != assets.constEnd(); ++it) {
        const QString name = it.key();
        if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
            qWarning() << "LocalStore: writeEditorSession bad asset name chars=" << name.size();
            removeEditorSessionDir(tmpDir);
            if (errorCode) {
                *errorCode = QStringLiteral("UPDATE_PERSIST_FAILED");
            }
            return false;
        }
        const QString assetPath = tmpDir + QLatin1Char('/') + name;
        if (it.value().isNull() || !it.value().save(assetPath, "PNG")) {
            qWarning() << "LocalStore: writeEditorSession asset failed chars=" << name.size();
            removeEditorSessionDir(tmpDir);
            if (errorCode) {
                *errorCode = QStringLiteral("UPDATE_PERSIST_FAILED");
            }
            return false;
        }
    }
    const QByteArray body = QJsonDocument(json).toJson(QJsonDocument::Compact);
    QFile meta(tmpDir + QStringLiteral("/session.json"));
    if (!meta.open(QIODevice::WriteOnly | QIODevice::Truncate) || meta.write(body) != body.size()) {
        qWarning() << "LocalStore: writeEditorSession json failed";
        removeEditorSessionDir(tmpDir);
        if (errorCode) {
            *errorCode = QStringLiteral("UPDATE_PERSIST_FAILED");
        }
        return false;
    }
    meta.close();
    removeEditorSessionDir(finalDir);
    if (!QDir().rename(tmpDir, finalDir)) {
        qWarning() << "LocalStore: writeEditorSession rename failed";
        removeEditorSessionDir(tmpDir);
        if (errorCode) {
            *errorCode = QStringLiteral("UPDATE_PERSIST_FAILED");
        }
        return false;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("editorSession"), true);
    settings.sync();
    qInfo() << "LocalStore: writeEditorSession assets=" << assets.size() << " jsonBytes=" << body.size();
    return true;
}

bool LocalStore::readEditorSession(QJsonObject *json, QImage *shot, QHash<QString, QImage> *assets,
                                   QString *errorCode)
{
    if (!json || !shot || !assets) {
        if (errorCode) {
            *errorCode = QStringLiteral("UPDATE_PERSIST_FAILED");
        }
        return false;
    }
    const QString dir = editorSessionDir();
    QImage loaded(dir + QStringLiteral("/shot.png"));
    if (loaded.isNull()) {
        qWarning() << "LocalStore: readEditorSession missing shot";
        if (errorCode) {
            *errorCode = QStringLiteral("UPDATE_PERSIST_FAILED");
        }
        return false;
    }
    QFile meta(dir + QStringLiteral("/session.json"));
    if (!meta.open(QIODevice::ReadOnly)) {
        qWarning() << "LocalStore: readEditorSession missing json";
        if (errorCode) {
            *errorCode = QStringLiteral("UPDATE_PERSIST_FAILED");
        }
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(meta.readAll());
    if (!doc.isObject()) {
        qWarning() << "LocalStore: readEditorSession json not object";
        if (errorCode) {
            *errorCode = QStringLiteral("UPDATE_PERSIST_FAILED");
        }
        return false;
    }
    *json = doc.object();
    *shot = loaded;
    assets->clear();
    const QDir folder(dir);
    const auto files = folder.entryList(QStringList{QStringLiteral("*.png")}, QDir::Files);
    for (const QString &name : files) {
        if (name == QLatin1String("shot.png")) {
            continue;
        }
        QImage image(dir + QLatin1Char('/') + name);
        if (image.isNull()) {
            qWarning() << "LocalStore: readEditorSession asset failed chars=" << name.size();
            if (errorCode) {
                *errorCode = QStringLiteral("UPDATE_PERSIST_FAILED");
            }
            return false;
        }
        assets->insert(name, image);
    }
    qInfo() << "LocalStore: readEditorSession shot=" << shot->size() << " assets=" << assets->size();
    return true;
}

void LocalStore::clearEditorSession()
{
    QSettings settings;
    settings.remove(QStringLiteral("editorSession"));
    settings.sync();
    removeEditorSessionDir(editorSessionDir());
    removeEditorSessionDir(settingsPath() + QStringLiteral("/editor-session-writing"));
    qInfo() << "LocalStore: editorSession cleared";
}
