#pragma once

#include <QImage>
#include <QString>

// ─── Ariadne's Thread [AT-0059] ─────────────────────
// What: Vision person mask applied as alpha; empty mask is failure
// Why:  PRD-03 forbids placing a photo with background
// Date: 2026-08-25
// Related: [AT-0058] CameraCapture.h, docs/PRD-03-photo-cutout.md
// ─────────────────────────────────────────────────────
QImage cutOutPerson(const QImage &source, QString *errorCode);
