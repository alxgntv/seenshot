#include "export/CloudPngEncoder.h"

#include <QBuffer>
#include <QDebug>
#include <QImageWriter>
#include <QRgb>
#include <QtMath>

namespace {

bool imageIsOpaque(const QImage &image)
{
    if (image.isNull()) {
        qWarning() << "CloudPngEncoder: opaque scan skipped, image is null";
        return true;
    }
    if (!image.hasAlphaChannel()) {
        qInfo() << "CloudPngEncoder: opaque scan no alpha channel format=" << image.format()
                << " width=" << image.width() << " height=" << image.height();
        return true;
    }
    const QImage src = image.convertToFormat(QImage::Format_ARGB32);
    const int width = src.width();
    const int height = src.height();
    qInfo() << "CloudPngEncoder: opaque scan start format=" << image.format()
            << " argbFormat=" << src.format() << " width=" << width << " height=" << height;
    for (int y = 0; y < height; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            if (qAlpha(line[x]) != 255) {
                qInfo() << "CloudPngEncoder: opaque scan found alpha=" << qAlpha(line[x])
                        << " x=" << x << " y=" << y;
                return false;
            }
        }
    }
    qInfo() << "CloudPngEncoder: opaque scan all pixels opaque width=" << width << " height=" << height;
    return true;
}

QByteArray writePng(const QImage &image, bool opaque)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, "png");
    writer.setCompression(9);
    if (!writer.write(image)) {
        qWarning() << "CloudPngEncoder: QImageWriter failed" << writer.errorString()
                   << " format=" << image.format() << " opaque=" << (opaque ? "yes" : "no");
        return {};
    }
    qInfo() << "CloudPngEncoder: wrote PNG bytes=" << bytes.size()
            << " width=" << image.width() << " height=" << image.height()
            << " format=" << image.format()
            << " opaque=" << (opaque ? "yes" : "no");
    return bytes;
}

QImage scaleToMegapixelCap(const QImage &source)
{
    const qint64 pixels = static_cast<qint64>(source.width()) * static_cast<qint64>(source.height());
    if (pixels <= CloudPngEncoder::kMaxMegapixels) {
        return source;
    }
    const double scale = qSqrt(static_cast<double>(CloudPngEncoder::kMaxMegapixels) / static_cast<double>(pixels));
    const QSize next(qMax(1, qRound(source.width() * scale)), qMax(1, qRound(source.height() * scale)));
    qInfo() << "CloudPngEncoder: scale megapixels" << pixels << "->" << next;
    return source.scaled(next, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QImage scaleLongSide(const QImage &source)
{
    const int longest = qMax(source.width(), source.height());
    if (longest <= CloudPngEncoder::kMaxLongSide) {
        return source;
    }
    qInfo() << "CloudPngEncoder: scale long side" << longest << "->" << CloudPngEncoder::kMaxLongSide;
    return source.scaled(CloudPngEncoder::kMaxLongSide, CloudPngEncoder::kMaxLongSide, Qt::KeepAspectRatio,
                         Qt::SmoothTransformation);
}

} // namespace

// ─── Ariadne's Thread [AT-0007] ─────────────────────
// What: Compress PNG until <= 8 MB using scale then RGB888
// Why:  STORAGE_FULL_FILE_TOO_BIG on a single frame is forbidden
// Date: 2026-08-25
// Related: [AT-0006] CloudPngEncoder.h
// ─────────────────────────────────────────────────────
// ─── Ariadne's Thread [AT-0214] ─────────────────────
// What: Encode one PNG for Save and Share; RGB888 when every pixel is opaque
// Why:  Disk and PUT must be the same bytes; unused alpha inflates opaque UI dumps
// Date: 2026-08-27
// Related: [AT-0007] CloudPngEncoder.cpp:encode, [AT-0215] AnnotateWindow.cpp:saveLocal
// ─────────────────────────────────────────────────────
QByteArray CloudPngEncoder::encode(const QImage &source, QString *errorCode)
{
    if (source.isNull()) {
        qWarning() << "CloudPngEncoder: source image is null";
        if (errorCode) {
            *errorCode = QStringLiteral("CLOUD_IMAGE_REJECTED");
        }
        return {};
    }

    QImage image = scaleLongSide(scaleToMegapixelCap(source));
    const bool opaque = imageIsOpaque(image);
    qInfo() << "CloudPngEncoder: after scale width=" << image.width() << " height=" << image.height()
            << " format=" << image.format() << " opaque=" << (opaque ? "yes" : "no");
    if (opaque) {
        image = image.convertToFormat(QImage::Format_RGB888);
        qInfo() << "CloudPngEncoder: converted RGB888 format=" << image.format()
                << " width=" << image.width() << " height=" << image.height();
    }
    QByteArray bytes = writePng(image, opaque);
    int guard = 0;
    while (bytes.size() > kMaxBytes && qMin(image.width(), image.height()) > kMinShortSide && guard < 16) {
        const QSize next(qMax(1, qRound(image.width() * 0.8)), qMax(1, qRound(image.height() * 0.8)));
        qInfo() << "CloudPngEncoder: shrink" << image.size() << "->" << next << " previousBytes=" << bytes.size();
        image = image.scaled(next, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (opaque) {
            image = image.convertToFormat(QImage::Format_RGB888);
        }
        bytes = writePng(image, opaque);
        ++guard;
    }

    if (bytes.size() > kMaxBytes) {
        qInfo() << "CloudPngEncoder: drop alpha and retry RGB888 bytes=" << bytes.size()
                << " width=" << image.width() << " height=" << image.height()
                << " format=" << image.format() << " opaque=" << (opaque ? "yes" : "no");
        image = image.convertToFormat(QImage::Format_RGB888);
        bytes = writePng(image, true);
    }

    if (bytes.isEmpty() || bytes.size() > kMaxBytes) {
        qCritical() << "CloudPngEncoder: still too large bytes=" << bytes.size()
                    << " width=" << image.width() << " height=" << image.height()
                    << " format=" << image.format() << " opaque=" << (opaque ? "yes" : "no");
        if (errorCode) {
            *errorCode = QStringLiteral("CLOUD_IMAGE_REJECTED");
        }
        return {};
    }

    qInfo() << "CloudPngEncoder: final bytes=" << bytes.size()
            << " width=" << image.width() << " height=" << image.height()
            << " format=" << image.format()
            << " opaque=" << (opaque ? "yes" : "no");
    return bytes;
}
