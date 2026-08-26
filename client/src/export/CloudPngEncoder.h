#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

// ─── Ariadne's Thread [AT-0006] ─────────────────────
// What: Encode a cloud PNG that always fits in 8 MB / 8 MP / 2560px
// Why:  One screenshot must always fit an empty 10 MB quota
// Date: 2026-08-25
// Related: client/src/export/CloudPngEncoder.cpp
// ─────────────────────────────────────────────────────
class CloudPngEncoder {
public:
    static constexpr int kMaxMegapixels = 8000000;
    static constexpr int kMaxLongSide = 2560;
    static constexpr int kMaxBytes = 8 * 1024 * 1024;
    static constexpr int kMinShortSide = 720;

    static QByteArray encode(const QImage &source, QString *errorCode);
};
