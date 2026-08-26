#pragma once

#include <QImage>
#include <QRect>
#include <QString>

// ─── Ariadne's Thread [AT-0008] ─────────────────────
// What: Capture backend interface for later Win/Linux ports
// Why:  Stage 1 is Mac only; other OS plug in later
// Date: 2026-08-25
// Related: client/src/capture/ScreenCaptureBackend.h
// ─────────────────────────────────────────────────────
class ICaptureBackend {
public:
    virtual ~ICaptureBackend() = default;
    virtual QImage captureRegion(const QRect &screenRect, QString *errorCode) = 0;
};
