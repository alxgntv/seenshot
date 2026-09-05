#include "export/CloudPngEncoder.h"

#include <QBuffer>
#include <QDebug>
#include <QImageWriter>
#include <QRgb>

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

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

// ─── Ariadne's Thread [AT-0550] ─────────────────────
// What: Build a CGImage from QImage bits for ImageIO PNG
// Why:  CGImageDestination is the same PNG encoder Screenshot.app uses
// Date: 2026-09-05
// Related: [AT-0550] CloudPngEncoder.mm:writePngImageIO, [AT-0214] CloudPngEncoder.mm:encode
// ─────────────────────────────────────────────────────
CGImageRef cgImageFromQImage(const QImage &source, bool opaque)
{
    const QImage src = source.convertToFormat(opaque ? QImage::Format_RGB32 : QImage::Format_ARGB32);
    if (src.isNull() || src.width() < 1 || src.height() < 1) {
        qWarning() << "CloudPngEncoder: CGImage skipped empty size=" << src.size() << " opaque=" << opaque;
        return nullptr;
    }
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    if (!space) {
        qWarning() << "CloudPngEncoder: CGColorSpaceCreateWithName sRGB failed";
        return nullptr;
    }
    const CGBitmapInfo info = opaque ? (kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Host)
                                     : (kCGImageAlphaFirst | kCGBitmapByteOrder32Host);
    QImage bitmap = src;
    CGContextRef ctx = CGBitmapContextCreate(bitmap.bits(), static_cast<size_t>(bitmap.width()),
                                             static_cast<size_t>(bitmap.height()), 8,
                                             static_cast<size_t>(bitmap.bytesPerLine()), space, info);
    CGColorSpaceRelease(space);
    if (!ctx) {
        qWarning() << "CloudPngEncoder: CGBitmapContextCreate failed size=" << src.size()
                   << " bpl=" << src.bytesPerLine() << " opaque=" << opaque << " format=" << src.format();
        return nullptr;
    }
    CGImageRef image = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    qInfo() << "CloudPngEncoder: CGImage" << (image != nullptr) << "size=" << src.size()
            << " bpl=" << src.bytesPerLine() << " opaque=" << opaque << " format=" << src.format()
            << " cg=" << (image ? static_cast<int>(CGImageGetWidth(image)) : 0) << "x"
            << (image ? static_cast<int>(CGImageGetHeight(image)) : 0)
            << " bpp=" << (image ? static_cast<int>(CGImageGetBitsPerPixel(image)) : 0);
    return image;
}

QByteArray writePngQt(const QImage &image, bool opaque)
{
    QImage out = image;
    if (opaque) {
        out = out.convertToFormat(QImage::Format_RGB888);
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, "png");
    writer.setCompression(9);
    if (!writer.write(out)) {
        qWarning() << "CloudPngEncoder: QImageWriter failed" << writer.errorString()
                   << " format=" << out.format() << " opaque=" << (opaque ? "yes" : "no");
        return {};
    }
    qInfo() << "CloudPngEncoder: Qt PNG bytes=" << bytes.size() << " width=" << out.width()
            << " height=" << out.height() << " format=" << out.format()
            << " opaque=" << (opaque ? "yes" : "no");
    return bytes;
}

QByteArray writePngImageIO(const QImage &image, bool opaque)
{
    CGImageRef cg = cgImageFromQImage(image, opaque);
    if (!cg) {
        qWarning() << "CloudPngEncoder: ImageIO skipped, CGImage is null size=" << image.size();
        return {};
    }
    NSMutableData *data = [NSMutableData data];
    CGImageDestinationRef dest =
        CGImageDestinationCreateWithData((__bridge CFMutableDataRef)data, CFSTR("public.png"), 1, nullptr);
    if (!dest) {
        qWarning() << "CloudPngEncoder: CGImageDestinationCreateWithData failed size=" << image.size();
        CGImageRelease(cg);
        return {};
    }
    CGImageDestinationAddImage(dest, cg, nullptr);
    const bool ok = CGImageDestinationFinalize(dest);
    const NSUInteger length = data.length;
    qInfo() << "CloudPngEncoder: ImageIO finalize ok=" << ok << " bytes=" << static_cast<int>(length)
            << " width=" << image.width() << " height=" << image.height() << " opaque=" << opaque
            << " cgBpp=" << static_cast<int>(CGImageGetBitsPerPixel(cg));
    CFRelease(dest);
    CGImageRelease(cg);
    if (!ok || length < 1) {
        qWarning() << "CloudPngEncoder: ImageIO PNG failed ok=" << ok << " bytes=" << static_cast<int>(length)
                   << " size=" << image.size();
        return {};
    }
    return QByteArray(static_cast<const char *>(data.bytes), static_cast<int>(length));
}

QByteArray writePng(const QImage &image, bool opaque)
{
    const QByteArray native = writePngImageIO(image, opaque);
    if (!native.isEmpty()) {
        qInfo() << "CloudPngEncoder: wrote PNG bytes=" << native.size() << " width=" << image.width()
                << " height=" << image.height() << " format=" << image.format()
                << " opaque=" << (opaque ? "yes" : "no") << " encoder=ImageIO";
        return native;
    }
    qWarning() << "CloudPngEncoder: ImageIO empty, falling back to QImageWriter size=" << image.size();
    const QByteArray qt = writePngQt(image, opaque);
    qInfo() << "CloudPngEncoder: wrote PNG bytes=" << qt.size() << " width=" << image.width()
            << " height=" << image.height() << " format=" << image.format()
            << " opaque=" << (opaque ? "yes" : "no") << " encoder=Qt";
    return qt;
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
// ─── Ariadne's Thread [AT-0550] ─────────────────────
// What: Keep full pixel size; ImageIO PNG; shrink only if over 8 MB quota cap
// Why:  2560px / 8 MP downsample made Retina shots soft; Screenshot.app does not downscale
// Date: 2026-09-05
// Related: [AT-0549] app→ScreenCaptureBackend.mm:captureWithManager, [AT-0215] AnnotateWindow.cpp:saveLocal
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

    QImage image = source;
    const bool opaque = imageIsOpaque(image);
    qInfo() << "CloudPngEncoder: encode start width=" << image.width() << " height=" << image.height()
            << " pixels=" << (static_cast<qint64>(image.width()) * static_cast<qint64>(image.height()))
            << " format=" << image.format() << " opaque=" << (opaque ? "yes" : "no")
            << " maxBytes=" << kMaxBytes;
    if (opaque) {
        image = image.convertToFormat(QImage::Format_RGB32);
        qInfo() << "CloudPngEncoder: converted RGB32 format=" << image.format()
                << " width=" << image.width() << " height=" << image.height();
    }
    QByteArray bytes = writePng(image, opaque);
    int guard = 0;
    while (bytes.size() > kMaxBytes && qMin(image.width(), image.height()) > kMinShortSide && guard < 16) {
        const QSize next(qMax(1, qRound(image.width() * 0.8)), qMax(1, qRound(image.height() * 0.8)));
        qInfo() << "CloudPngEncoder: shrink" << image.size() << "->" << next << " previousBytes=" << bytes.size();
        image = image.scaled(next, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (opaque) {
            image = image.convertToFormat(QImage::Format_RGB32);
        }
        bytes = writePng(image, opaque);
        ++guard;
    }

    if (bytes.size() > kMaxBytes) {
        qInfo() << "CloudPngEncoder: drop alpha and retry RGB32 bytes=" << bytes.size()
                << " width=" << image.width() << " height=" << image.height()
                << " format=" << image.format() << " opaque=" << (opaque ? "yes" : "no");
        image = image.convertToFormat(QImage::Format_RGB32);
        bytes = writePng(image, true);
    }

    if (bytes.isEmpty() || bytes.size() > kMaxBytes) {
        qCritical() << "CloudPngEncoder: still too large bytes=" << bytes.size()
                    << " width=" << image.width() << " height=" << image.height()
                    << " format=" << image.format()
                    << " opaque=" << (opaque ? "yes" : "no");
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
