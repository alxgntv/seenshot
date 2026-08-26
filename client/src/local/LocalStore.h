#pragma once

#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QKeySequence>
#include <QString>

// ─── Ariadne's Thread [AT-0021] ─────────────────────
// What: Local settings and temp capture paths
// Why:  Drafts go to NSTemporaryDirectory equivalent
// Date: 2026-08-25
// Related: client/src/local/LocalStore.cpp
// ─────────────────────────────────────────────────────
class LocalStore {
public:
    static QString settingsPath();
    static QString tempCapturePath();
    static void removeTempCapture(const QString &path);
    static bool firstRunCompleted();
    static void setFirstRunCompleted();
    static QString hotkeySpec();
    static void setHotkeySpec(const QString &spec);
    static QString fullScreenHotkeySpec();
    static void setFullScreenHotkeySpec(const QString &spec);
    static QKeySequence keySequenceFromSpec(const QString &spec);
    static QString specFromKeySequence(const QKeySequence &sequence);
    static QString nativeHotkeyLabel(const QString &spec);
    static QString pendingSignInEmail();
    static void setPendingSignInEmail(const QString &email);
    static void clearPendingSignInEmail();
    static bool hasEditorSession();
    static bool writeEditorSession(const QJsonObject &json, const QImage &shot,
                                   const QHash<QString, QImage> &assets, QString *errorCode);
    static bool readEditorSession(QJsonObject *json, QImage *shot, QHash<QString, QImage> *assets,
                                  QString *errorCode);
    static void clearEditorSession();
};
