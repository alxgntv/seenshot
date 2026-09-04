#pragma once

#include <QImage>
#include <QList>
#include <QRect>
#include <QString>

// ─── Ariadne's Thread [AT-0391] ─────────────────────
// What: On-device sensitive-region hits for auto Blur
// Why:  Faces and OCR stay on-device; types are applied by the editor
// Date: 2026-09-03
// Related: [AT-0392] app→SensitiveRedact.mm:detectSensitive, [AT-0390] app→LocalStore.h
// ─────────────────────────────────────────────────────
enum class SensitiveKind {
    None = 0,
    Face = 1,
    Phone = 2,
    Email = 3,
    ApiKey = 4,
};

constexpr int kSensitiveKindFace = 1 << 0;
constexpr int kSensitiveKindPhone = 1 << 1;
constexpr int kSensitiveKindEmail = 1 << 2;
constexpr int kSensitiveKindApiKey = 1 << 3;
constexpr int kSensitiveKindAll =
    kSensitiveKindFace | kSensitiveKindPhone | kSensitiveKindEmail | kSensitiveKindApiKey;

struct SensitiveHit {
    QRect rect;
    SensitiveKind kind = SensitiveKind::None;
};

QList<SensitiveHit> detectSensitive(const QImage &image, int kindMask, QString *errorCode);
