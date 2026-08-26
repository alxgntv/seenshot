#include "camera/PersonCutout.h"

#include <QDebug>

#import <CoreGraphics/CoreGraphics.h>
#import <CoreVideo/CoreVideo.h>
#import <Vision/Vision.h>

namespace {

CGImageRef cgImageFromQImage(const QImage &source)
{
    const QImage src = source.convertToFormat(QImage::Format_ARGB32);
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(const_cast<uchar *>(src.bits()), src.width(), src.height(), 8,
                                             src.bytesPerLine(), space,
                                             kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
    CGImageRef image = ctx ? CGBitmapContextCreateImage(ctx) : nullptr;
    if (ctx) {
        CGContextRelease(ctx);
    }
    if (space) {
        CGColorSpaceRelease(space);
    }
    return image;
}

QImage cropOpaque(const QImage &src)
{
    int minX = src.width();
    int minY = src.height();
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < src.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        for (int x = 0; x < src.width(); ++x) {
            if (qAlpha(line[x]) > 16) {
                minX = qMin(minX, x);
                minY = qMin(minY, y);
                maxX = qMax(maxX, x);
                maxY = qMax(maxY, y);
            }
        }
    }
    if (maxX < minX || maxY < minY) {
        qWarning() << "cutOutPerson: crop found no opaque pixels";
        return QImage();
    }
    const QRect box(minX, minY, maxX - minX + 1, maxY - minY + 1);
    qInfo() << "cutOutPerson: crop" << box << "from" << src.size();
    return src.copy(box);
}

} // namespace

QImage cutOutPerson(const QImage &source, QString *errorCode)
{
    if (source.isNull()) {
        qWarning() << "cutOutPerson: null source";
        if (errorCode) {
            *errorCode = QStringLiteral("PHOTO_CUTOUT_FAILED");
        }
        return QImage();
    }
    CGImageRef cg = cgImageFromQImage(source);
    if (!cg) {
        qWarning() << "cutOutPerson: CGImage failed";
        if (errorCode) {
            *errorCode = QStringLiteral("PHOTO_CUTOUT_FAILED");
        }
        return QImage();
    }
    VNGeneratePersonSegmentationRequest *request = [[VNGeneratePersonSegmentationRequest alloc] init];
    request.qualityLevel = VNGeneratePersonSegmentationRequestQualityLevelBalanced;
    request.outputPixelFormat = kCVPixelFormatType_OneComponent8;
    VNImageRequestHandler *handler = [[VNImageRequestHandler alloc] initWithCGImage:cg options:@{}];
    NSError *error = nil;
    const BOOL ok = [handler performRequests:@[request] error:&error];
    CGImageRelease(cg);
    if (!ok) {
        qWarning() << "cutOutPerson: Vision failed" << QString::fromNSString(error.localizedDescription);
        if (errorCode) {
            *errorCode = QStringLiteral("PHOTO_CUTOUT_FAILED");
        }
        return QImage();
    }
    VNPixelBufferObservation *obs = nil;
    for (VNObservation *item in request.results) {
        if ([item isKindOfClass:[VNPixelBufferObservation class]]) {
            obs = (VNPixelBufferObservation *)item;
            break;
        }
    }
    if (!obs) {
        qWarning() << "cutOutPerson: no mask observation";
        if (errorCode) {
            *errorCode = QStringLiteral("PHOTO_NO_PERSON");
        }
        return QImage();
    }
    CVPixelBufferRef mask = obs.pixelBuffer;
    CVPixelBufferLockBaseAddress(mask, kCVPixelBufferLock_ReadOnly);
    const size_t maskW = CVPixelBufferGetWidth(mask);
    const size_t maskH = CVPixelBufferGetHeight(mask);
    const size_t stride = CVPixelBufferGetBytesPerRow(mask);
    const uint8_t *base = static_cast<const uint8_t *>(CVPixelBufferGetBaseAddress(mask));
    QImage rgba = source.convertToFormat(QImage::Format_ARGB32);
    int opaque = 0;
    for (int y = 0; y < rgba.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(rgba.scanLine(y));
        const int my = qBound(0, qRound(static_cast<qreal>(y) * static_cast<qreal>(maskH) / rgba.height()),
                              static_cast<int>(maskH) - 1);
        const uint8_t *maskLine = base + static_cast<size_t>(my) * stride;
        for (int x = 0; x < rgba.width(); ++x) {
            const int mx = qBound(0, qRound(static_cast<qreal>(x) * static_cast<qreal>(maskW) / rgba.width()),
                                  static_cast<int>(maskW) - 1);
            const int cover = maskLine[mx];
            if (cover > 16) {
                ++opaque;
            }
            const QRgb px = line[x];
            line[x] = qRgba(qRed(px), qGreen(px), qBlue(px), qBound(0, cover, 255));
        }
    }
    CVPixelBufferUnlockBaseAddress(mask, kCVPixelBufferLock_ReadOnly);
    qInfo() << "cutOutPerson: mask=" << (int)maskW << "x" << (int)maskH << "opaque=" << opaque
            << "source=" << rgba.size();
    if (opaque < 200) {
        qWarning() << "cutOutPerson: empty person mask";
        if (errorCode) {
            *errorCode = QStringLiteral("PHOTO_NO_PERSON");
        }
        return QImage();
    }
    const QImage cropped = cropOpaque(rgba);
    if (cropped.isNull()) {
        if (errorCode) {
            *errorCode = QStringLiteral("PHOTO_NO_PERSON");
        }
        return QImage();
    }
    qInfo() << "cutOutPerson: result" << cropped.size();
    return cropped;
}
