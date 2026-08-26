#include "export/CloudPngEncoder.h"

#include <QBuffer>
#include <QDebug>
#include <QImageWriter>
#include <QtMath>

namespace {

QByteArray writePng(const QImage &image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, "png");
    writer.setCompression(9);
    if (!writer.write(image)) {
        qWarning() << "CloudPngEncoder: QImageWriter failed" << writer.errorString();
        return {};
    }
    qInfo() << "CloudPngEncoder: wrote PNG bytes=" << bytes.size()
            << " size=" << image.width() << "x" << image.height()
            << " format=" << image.format();
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
    QByteArray bytes = writePng(image);
    int guard = 0;
    while (bytes.size() > kMaxBytes && qMin(image.width(), image.height()) > kMinShortSide && guard < 16) {
        const QSize next(qMax(1, qRound(image.width() * 0.8)), qMax(1, qRound(image.height() * 0.8)));
        qInfo() << "CloudPngEncoder: shrink" << image.size() << "->" << next << " previousBytes=" << bytes.size();
        image = image.scaled(next, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        bytes = writePng(image);
        ++guard;
    }

    if (bytes.size() > kMaxBytes) {
        qInfo() << "CloudPngEncoder: drop alpha and retry RGB888 bytes=" << bytes.size();
        image = image.convertToFormat(QImage::Format_RGB888);
        bytes = writePng(image);
    }

    if (bytes.isEmpty() || bytes.size() > kMaxBytes) {
        qCritical() << "CloudPngEncoder: still too large bytes=" << bytes.size();
        if (errorCode) {
            *errorCode = QStringLiteral("CLOUD_IMAGE_REJECTED");
        }
        return {};
    }

    qInfo() << "CloudPngEncoder: final bytes=" << bytes.size() << " dim=" << image.width() << "x" << image.height();
    return bytes;
}
