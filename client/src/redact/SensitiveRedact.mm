#include "redact/SensitiveRedact.h"

#include <QDebug>
#include <QList>
#include <QRect>

#import <CoreGraphics/CoreGraphics.h>
#import <CoreImage/CoreImage.h>
#import <Foundation/Foundation.h>
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
    qInfo() << "detectSensitive: CGImage" << (image != nullptr) << "size=" << src.size()
            << "bytesPerLine=" << src.bytesPerLine();
    return image;
}

QRect pixelRectFromVisionBox(CGRect box, int width, int height)
{
    const qreal x = static_cast<qreal>(box.origin.x) * static_cast<qreal>(width);
    const qreal w = static_cast<qreal>(box.size.width) * static_cast<qreal>(width);
    const qreal h = static_cast<qreal>(box.size.height) * static_cast<qreal>(height);
    const qreal y = (1.0 - static_cast<qreal>(box.origin.y) - static_cast<qreal>(box.size.height))
                    * static_cast<qreal>(height);
    QRect rect(qRound(x), qRound(y), qRound(w), qRound(h));
    const QRect bounds(0, 0, width, height);
    const QRect clipped = rect.intersected(bounds);
    qInfo() << "detectSensitive: visionBox" << box.origin.x << box.origin.y << box.size.width << box.size.height
            << "-> pixel" << clipped << "image=" << width << "x" << height;
    return clipped;
}

QRect paddedTextRect(const QRect &rect, int width, int height)
{
    QRect padded = rect.adjusted(-2, -2, 2, 2);
    return padded.intersected(QRect(0, 0, width, height));
}

qreal rectIou(const QRect &a, const QRect &b)
{
    const QRect inter = a.intersected(b);
    if (inter.isEmpty()) {
        return 0;
    }
    const qreal interArea = static_cast<qreal>(inter.width()) * static_cast<qreal>(inter.height());
    const qreal unionArea = static_cast<qreal>(a.width()) * static_cast<qreal>(a.height())
                            + static_cast<qreal>(b.width()) * static_cast<qreal>(b.height()) - interArea;
    if (unionArea <= 0) {
        return 0;
    }
    return interArea / unionArea;
}

// ─── Ariadne's Thread [AT-0400] ─────────────────────
// What: Drop bulky Vision/CIDetector faces unless Apple landmarks/eyes confirm a face
// Why:  Page art and UI were tagged as faces; union+20% pad then blurred the form
// Date: 2026-09-03
// Related: [AT-0399] SensitiveRedact.mm:detectSensitive, Apple VNDetectFaceLandmarksRequest
// ─────────────────────────────────────────────────────
bool landmarkRegionHasPoints(VNFaceLandmarkRegion2D *region)
{
    return region != nil && region.pointCount > 0;
}

bool landmarksLookLikeFace(VNFaceLandmarks2D *landmarks)
{
    if (!landmarks) {
        return false;
    }
    int parts = 0;
    if (landmarkRegionHasPoints(landmarks.leftEye)) {
        ++parts;
    }
    if (landmarkRegionHasPoints(landmarks.rightEye)) {
        ++parts;
    }
    if (landmarkRegionHasPoints(landmarks.nose)) {
        ++parts;
    }
    if (landmarkRegionHasPoints(landmarks.outerLips)) {
        ++parts;
    }
    qInfo() << "detectSensitive: landmark parts=" << parts << "confidence=" << landmarks.confidence;
    return parts >= 2;
}

bool visionFaceHasLandmarks(CGImageRef image, VNFaceObservation *faceObs)
{
    if (!image || !faceObs) {
        qWarning() << "detectSensitive: landmarks missing image/observation";
        return false;
    }
    VNDetectFaceLandmarksRequest *request = [[VNDetectFaceLandmarksRequest alloc] init];
    request.inputFaceObservations = @[faceObs];
    VNImageRequestHandler *handler = [[VNImageRequestHandler alloc] initWithCGImage:image options:@{}];
    NSError *error = nil;
    const BOOL ok = [handler performRequests:@[request] error:&error];
    if (!ok) {
        qWarning() << "detectSensitive: VNDetectFaceLandmarksRequest failed"
                   << (error ? QString::fromNSString(error.localizedDescription) : QStringLiteral("nil"));
        return false;
    }
    for (VNFaceObservation *obs in request.results) {
        if (landmarksLookLikeFace(obs.landmarks)) {
            return true;
        }
    }
    qInfo() << "detectSensitive: landmarks empty results=" << static_cast<int>(request.results.count);
    return false;
}

// ─── Ariadne's Thread [AT-0408] ─────────────────────
// What: Score bulky faces against the full shot; keep faces down to 8px
// Why:  Tile-local 0.18 frac treated avatars as bulky; 16px dropped icon faces
// Date: 2026-09-03
// Related: [AT-0400] SensitiveRedact.mm:acceptVisionFace, [AT-0408] SensitiveRedact.mm:lanczosScaleCGImage
// ─────────────────────────────────────────────────────
bool acceptVisionFace(CGImageRef target, VNFaceObservation *obs, const QRect &visionBox, int visionW,
                      int visionH, const QRect &fullBox, int fullWidth, int fullHeight)
{
    const int side = qMin(fullBox.width(), fullBox.height());
    if (side < 8) {
        qInfo() << "detectSensitive: skip small vision face side=" << side << "full=" << fullBox
                << "vision=" << visionBox << "visionSize=" << visionW << "x" << visionH;
        return false;
    }
    const qreal fracW = static_cast<qreal>(fullBox.width()) / static_cast<qreal>(qMax(1, fullWidth));
    const qreal fracH = static_cast<qreal>(fullBox.height()) / static_cast<qreal>(qMax(1, fullHeight));
    const qreal fracSide = static_cast<qreal>(side) / static_cast<qreal>(qMax(1, qMin(fullWidth, fullHeight)));
    const bool bulky = fracW > 0.35 || fracH > 0.35 || fracSide > 0.18;
    qInfo() << "detectSensitive: vision face side=" << side << "fracW=" << fracW << "fracH=" << fracH
            << "fracSide=" << fracSide << "bulky=" << bulky << "confidence=" << (obs ? obs.confidence : 0)
            << "full=" << fullBox << "vision=" << visionBox << "visionSize=" << visionW << "x" << visionH;
    if (!bulky) {
        return true;
    }
    if (!visionFaceHasLandmarks(target, obs)) {
        qInfo() << "detectSensitive: skip bulky face without landmarks" << fullBox;
        return false;
    }
    qInfo() << "detectSensitive: bulky face kept with landmarks" << fullBox;
    return true;
}

void mergeFaceBox(QList<QRect> *boxes, const QRect &box)
{
    if (!boxes || box.width() < 1 || box.height() < 1) {
        return;
    }
    for (int i = 0; i < boxes->size(); ++i) {
        const qreal iou = rectIou(boxes->at(i), box);
        if (iou <= 0.4) {
            continue;
        }
        const int oldArea = boxes->at(i).width() * boxes->at(i).height();
        const int newArea = box.width() * box.height();
        const QRect kept = newArea < oldArea ? box : boxes->at(i);
        qInfo() << "detectSensitive: merge face keep-smaller iou=" << iou << boxes->at(i) << "+" << box
                << "->" << kept;
        (*boxes)[i] = kept;
        return;
    }
    boxes->append(box);
    qInfo() << "detectSensitive: keep face" << box << "count=" << boxes->size();
}

void appendUniqueTile(QList<QRect> *tiles, const QRect &tile, int minW, int minH)
{
    if (!tiles) {
        qWarning() << "detectSensitive: appendUniqueTile missing tiles" << tile;
        return;
    }
    if (tile.width() < minW || tile.height() < minH) {
        qInfo() << "detectSensitive: skip tiny tile" << tile << "min=" << minW << "x" << minH;
        return;
    }
    if (tiles->contains(tile)) {
        qInfo() << "detectSensitive: skip duplicate tile" << tile;
        return;
    }
    tiles->append(tile);
    qInfo() << "detectSensitive: add face tile" << tile << "count=" << tiles->size();
}

void appendGridTiles(QList<QRect> *tiles, int width, int height, int cols, int rows, int overlapX, int overlapY,
                     int minSide)
{
    if (!tiles || width < 1 || height < 1 || cols < 1 || rows < 1) {
        qWarning() << "detectSensitive: appendGridTiles invalid" << width << "x" << height << "grid=" << cols
                   << "x" << rows;
        return;
    }
    const int tileW = qMax(1, (width + overlapX * (cols - 1)) / cols);
    const int tileH = qMax(1, (height + overlapY * (rows - 1)) / rows);
    qInfo() << "detectSensitive: grid" << cols << "x" << rows << "tile=" << tileW << "x" << tileH
            << "overlap=" << overlapX << "x" << overlapY << "minSide=" << minSide;
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const int x = qMin(width - 1, col * qMax(1, tileW - overlapX));
            const int y = qMin(height - 1, row * qMax(1, tileH - overlapY));
            const int w = qMin(tileW, width - x);
            const int h = qMin(tileH, height - y);
            appendUniqueTile(tiles, QRect(x, y, w, h), minSide, minSide);
        }
    }
}

QList<QRect> faceSearchTiles(int width, int height)
{
    QList<QRect> tiles;
    appendUniqueTile(&tiles, QRect(0, 0, width, height), 1, 1);
    const int stripH = qBound(64, height / 4, height);
    appendUniqueTile(&tiles, QRect(0, 0, width, stripH), 64, 64);
    appendGridTiles(&tiles, width, height, 3, 2, qMax(24, width / 12), qMax(24, height / 12), 64);
    appendGridTiles(&tiles, width, height, 6, 4, qMax(16, width / 24), qMax(16, height / 24), 48);
    const int iconH = qBound(48, height / 8, 128);
    appendGridTiles(&tiles, width, iconH, 10, 1, qMax(8, width / 40), 0, 32);
    qInfo() << "detectSensitive: face tiles=" << tiles.size() << "image=" << width << "x" << height
            << "stripH=" << stripH << "iconH=" << iconH;
    return tiles;
}

// ─── Ariadne's Thread [AT-0408] ─────────────────────
// What: Lanczos-upsample small face tiles and map Vision boxes back to shot pixels
// Why:  VNDetectFaceRectanglesRequest misses avatar-scale and icon-scale faces at 1x
// Date: 2026-09-03
// Related: [AT-0399] SensitiveRedact.mm:collectVisionFaces, Apple CIFilter CILanczosScaleTransform
// ─────────────────────────────────────────────────────
CGImageRef lanczosScaleCGImage(CGImageRef source, CGFloat scale)
{
    if (!source || scale <= 1.0001) {
        qInfo() << "detectSensitive: lanczos skip source=" << (source != nullptr) << "scale=" << scale;
        return nullptr;
    }
    CIImage *ci = [CIImage imageWithCGImage:source];
    if (!ci) {
        qWarning() << "detectSensitive: lanczos CIImage failed scale=" << scale;
        return nullptr;
    }
    CIFilter *filter = [CIFilter filterWithName:@"CILanczosScaleTransform"];
    if (!filter) {
        qWarning() << "detectSensitive: CILanczosScaleTransform missing";
        return nullptr;
    }
    [filter setValue:ci forKey:kCIInputImageKey];
    [filter setValue:@(scale) forKey:kCIInputScaleKey];
    [filter setValue:@1.0 forKey:kCIInputAspectRatioKey];
    CIImage *out = filter.outputImage;
    if (!out) {
        qWarning() << "detectSensitive: lanczos outputImage nil scale=" << scale;
        return nullptr;
    }
    CIContext *ctx = [CIContext contextWithOptions:nil];
    const CGRect extent = out.extent;
    CGImageRef img = [ctx createCGImage:out fromRect:extent];
    qInfo() << "detectSensitive: lanczos scale=" << scale << "in=" << static_cast<int>(CGImageGetWidth(source))
            << "x" << static_cast<int>(CGImageGetHeight(source)) << "out="
            << (img ? static_cast<int>(CGImageGetWidth(img)) : 0) << "x"
            << (img ? static_cast<int>(CGImageGetHeight(img)) : 0) << "extentOrigin=" << extent.origin.x
            << extent.origin.y << "extentSize=" << extent.size.width << "x" << extent.size.height;
    return img;
}

QRect mapVisionBoxToTile(const QRect &visionBox, int visionW, int visionH, int tileW, int tileH)
{
    if (visionW <= 0 || visionH <= 0 || tileW <= 0 || tileH <= 0) {
        qWarning() << "detectSensitive: mapVisionBoxToTile invalid vision=" << visionW << "x" << visionH
                   << "tile=" << tileW << "x" << tileH << "box=" << visionBox;
        return QRect();
    }
    const qreal sx = static_cast<qreal>(tileW) / static_cast<qreal>(visionW);
    const qreal sy = static_cast<qreal>(tileH) / static_cast<qreal>(visionH);
    QRect local(qRound(static_cast<qreal>(visionBox.x()) * sx), qRound(static_cast<qreal>(visionBox.y()) * sy),
                qRound(static_cast<qreal>(visionBox.width()) * sx),
                qRound(static_cast<qreal>(visionBox.height()) * sy));
    const QRect clipped = local.intersected(QRect(0, 0, tileW, tileH));
    qInfo() << "detectSensitive: map visionBox" << visionBox << "vision=" << visionW << "x" << visionH
            << "tile=" << tileW << "x" << tileH << "sx=" << sx << "sy=" << sy << "->" << clipped;
    return clipped;
}

void runVisionFaceRequest(CGImageRef visionSource, const QRect &tile, int tileW, int tileH, int fullWidth,
                          int fullHeight, QList<QRect> *boxes)
{
    if (!visionSource || !boxes) {
        qWarning() << "detectSensitive: runVisionFaceRequest missing source/boxes tile=" << tile;
        return;
    }
    const int visionW = static_cast<int>(CGImageGetWidth(visionSource));
    const int visionH = static_cast<int>(CGImageGetHeight(visionSource));
    if (visionW < 1 || visionH < 1 || tileW < 1 || tileH < 1) {
        qWarning() << "detectSensitive: runVisionFaceRequest empty size vision=" << visionW << "x" << visionH
                   << "tilePx=" << tileW << "x" << tileH << "tile=" << tile;
        return;
    }
    VNDetectFaceRectanglesRequest *request = [[VNDetectFaceRectanglesRequest alloc] init];
    request.revision = VNDetectFaceRectanglesRequestRevision3;
    VNImageRequestHandler *handler = [[VNImageRequestHandler alloc] initWithCGImage:visionSource options:@{}];
    NSError *error = nil;
    const BOOL ok = [handler performRequests:@[request] error:&error];
    if (!ok) {
        qWarning() << "detectSensitive: Vision faces failed tile=" << tile << "vision=" << visionW << "x"
                   << visionH << (error ? QString::fromNSString(error.localizedDescription) : QStringLiteral("nil"));
        return;
    }
    int found = 0;
    for (VNFaceObservation *obs in request.results) {
        const QRect visionBox = pixelRectFromVisionBox(obs.boundingBox, visionW, visionH);
        const QRect localBox = mapVisionBoxToTile(visionBox, visionW, visionH, tileW, tileH);
        QRect fullBox = localBox.translated(tile.topLeft());
        fullBox = fullBox.intersected(QRect(0, 0, fullWidth, fullHeight));
        qInfo() << "detectSensitive: vision face tile=" << tile << "confidence=" << obs.confidence
                << "visionBox=" << visionBox << "local=" << localBox << "full=" << fullBox;
        if (!acceptVisionFace(visionSource, obs, visionBox, visionW, visionH, fullBox, fullWidth, fullHeight)) {
            continue;
        }
        mergeFaceBox(boxes, fullBox);
        ++found;
    }
    qInfo() << "detectSensitive: vision tile done" << tile << "vision=" << visionW << "x" << visionH
            << "raw=" << found << "merged=" << boxes->size();
}

void collectVisionFaces(CGImageRef image, const QRect &tile, int fullWidth, int fullHeight, QList<QRect> *boxes)
{
    if (!image || !boxes) {
        qWarning() << "detectSensitive: collectVisionFaces missing image/boxes tile=" << tile;
        return;
    }
    CGImageRef crop = nullptr;
    CGImageRef visionSource = image;
    int tileW = fullWidth;
    int tileH = fullHeight;
    if (tile != QRect(0, 0, fullWidth, fullHeight)) {
        crop = CGImageCreateWithImageInRect(image, CGRectMake(tile.x(), tile.y(), tile.width(), tile.height()));
        if (!crop) {
            qWarning() << "detectSensitive: tile CGImage failed" << tile;
            return;
        }
        visionSource = crop;
        tileW = static_cast<int>(CGImageGetWidth(crop));
        tileH = static_cast<int>(CGImageGetHeight(crop));
    }
    const int minSide = qMin(tileW, tileH);
    const int kUpsampleMin = 512;
    CGImageRef scaled = nullptr;
    if (minSide > 0 && minSide < kUpsampleMin) {
        const CGFloat scale = static_cast<CGFloat>(kUpsampleMin) / static_cast<CGFloat>(minSide);
        qInfo() << "detectSensitive: upsample tile" << tile << "minSide=" << minSide << "scale=" << scale;
        scaled = lanczosScaleCGImage(visionSource, scale);
        if (scaled) {
            visionSource = scaled;
        } else {
            qWarning() << "detectSensitive: upsample failed tile=" << tile << "keep 1x minSide=" << minSide;
        }
    }
    runVisionFaceRequest(visionSource, tile, tileW, tileH, fullWidth, fullHeight, boxes);
    if (scaled) {
        CGImageRelease(scaled);
    }
    if (crop) {
        CGImageRelease(crop);
    }
}

void collectVisionFacesBoosted(CGImageRef image, int fullWidth, int fullHeight, QList<QRect> *boxes)
{
    if (!image || !boxes) {
        qWarning() << "detectSensitive: collectVisionFacesBoosted missing image/boxes";
        return;
    }
    const int minSide = qMin(fullWidth, fullHeight);
    const int maxSide = qMax(fullWidth, fullHeight);
    if (minSide < 1 || maxSide < 1) {
        qWarning() << "detectSensitive: collectVisionFacesBoosted empty image=" << fullWidth << "x" << fullHeight;
        return;
    }
    CGFloat scale = 3.0;
    if (static_cast<CGFloat>(maxSide) * scale > 4096.0) {
        scale = 4096.0 / static_cast<CGFloat>(maxSide);
    }
    if (scale < 1.25) {
        qInfo() << "detectSensitive: skip full-frame boost scale=" << scale << "maxSide=" << maxSide;
        return;
    }
    qInfo() << "detectSensitive: full-frame boost scale=" << scale << "image=" << fullWidth << "x" << fullHeight;
    CGImageRef boosted = lanczosScaleCGImage(image, scale);
    if (!boosted) {
        qWarning() << "detectSensitive: full-frame boost failed scale=" << scale;
        return;
    }
    runVisionFaceRequest(boosted, QRect(0, 0, fullWidth, fullHeight), fullWidth, fullHeight, fullWidth, fullHeight,
                         boxes);
    CGImageRelease(boosted);
}

void collectCoreImageFaces(CGImageRef image, int width, int height, QList<QRect> *boxes)
{
    if (!image || !boxes) {
        qWarning() << "detectSensitive: collectCoreImageFaces missing image/boxes";
        return;
    }
    CIImage *ciImage = [CIImage imageWithCGImage:image];
    if (!ciImage) {
        qWarning() << "detectSensitive: CIImage failed size=" << width << "x" << height;
        return;
    }
    NSDictionary *opts = @{
        CIDetectorAccuracy: CIDetectorAccuracyHigh,
        CIDetectorMinFeatureSize: @0.01,
        CIDetectorNumberOfAngles: @7,
    };
    CIDetector *detector = [CIDetector detectorOfType:CIDetectorTypeFace context:nil options:opts];
    if (!detector) {
        qWarning() << "detectSensitive: CIDetector create failed";
        return;
    }
    NSArray<CIFeature *> *features = [detector featuresInImage:ciImage];
    qInfo() << "detectSensitive: CIDetector features=" << static_cast<int>(features.count)
            << "minFeatureSize=0.01 angles=7";
    const CGRect extent = ciImage.extent;
    for (CIFeature *feature in features) {
        if (![feature isKindOfClass:[CIFaceFeature class]]) {
            continue;
        }
        const CGRect bounds = feature.bounds;
        const qreal x = bounds.origin.x - extent.origin.x;
        const qreal h = bounds.size.height;
        const qreal y = extent.origin.y + extent.size.height - bounds.origin.y - h;
        QRect box(qRound(x), qRound(y), qRound(bounds.size.width), qRound(h));
        box = box.intersected(QRect(0, 0, width, height));
        const int side = qMin(box.width(), box.height());
        const qreal fracSide = static_cast<qreal>(side) / static_cast<qreal>(qMax(1, qMin(width, height)));
        CIFaceFeature *face = (CIFaceFeature *)feature;
        qInfo() << "detectSensitive: CIDetector face side=" << side << "fracSide=" << fracSide
                << "leftEye=" << face.hasLeftEyePosition << "rightEye=" << face.hasRightEyePosition
                << "mouth=" << face.hasMouthPosition << box;
        if (side < 8) {
            qInfo() << "detectSensitive: skip small CIDetector face side=" << side;
            continue;
        }
        if (fracSide > 0.18 && (!face.hasLeftEyePosition || !face.hasRightEyePosition)) {
            qInfo() << "detectSensitive: skip bulky CIDetector face without both eyes" << box;
            continue;
        }
        mergeFaceBox(boxes, box);
    }
}

struct TextLine {
    VNRecognizedTextObservation *observation = nil;
    VNRecognizedText *text = nil;
    NSUInteger start = 0;
    NSUInteger length = 0;
};

QRect rectForJoinedRange(const QList<TextLine> &lines, NSRange range, int width, int height)
{
    QRect unionRect;
    bool any = false;
    const NSUInteger matchStart = range.location;
    const NSUInteger matchEnd = range.location + range.length;
    for (const TextLine &line : lines) {
        const NSUInteger lineEnd = line.start + line.length;
        if (lineEnd <= matchStart || line.start >= matchEnd || !line.text) {
            continue;
        }
        const NSUInteger localStart = matchStart > line.start ? (matchStart - line.start) : 0;
        NSUInteger localEnd = matchEnd > line.start ? (matchEnd - line.start) : 0;
        if (localEnd > line.length) {
            localEnd = line.length;
        }
        if (localEnd <= localStart) {
            continue;
        }
        NSRange local = NSMakeRange(localStart, localEnd - localStart);
        NSError *boxError = nil;
        VNRectangleObservation *boxObs = [line.text boundingBoxForRange:local error:&boxError];
        QRect piece;
        if (boxObs) {
            piece = pixelRectFromVisionBox(boxObs.boundingBox, width, height);
        } else {
            qWarning() << "detectSensitive: boundingBoxForRange failed"
                       << (boxError ? QString::fromNSString(boxError.localizedDescription) : QStringLiteral("nil"))
                       << "localStart=" << static_cast<qulonglong>(localStart)
                       << "localLen=" << static_cast<qulonglong>(local.length);
            piece = pixelRectFromVisionBox(line.observation.boundingBox, width, height);
        }
        if (piece.isEmpty()) {
            continue;
        }
        unionRect = any ? unionRect.united(piece) : piece;
        any = true;
    }
    if (!any) {
        qWarning() << "detectSensitive: no pixel rect for joined range loc="
                   << static_cast<qulonglong>(range.location) << "len=" << static_cast<qulonglong>(range.length);
        return QRect();
    }
    return paddedTextRect(unionRect, width, height);
}

void appendHit(QList<SensitiveHit> *hits, const QRect &rect, SensitiveKind kind, const QString &sample)
{
    if (!hits) {
        qWarning() << "detectSensitive: appendHit null list kind=" << static_cast<int>(kind);
        return;
    }
    if (rect.width() < 1 || rect.height() < 1) {
        qInfo() << "detectSensitive: skip empty hit kind=" << static_cast<int>(kind) << "sampleChars=" << sample.size();
        return;
    }
    SensitiveHit hit;
    hit.rect = rect;
    hit.kind = kind;
    hits->append(hit);
    qInfo() << "detectSensitive: hit kind=" << static_cast<int>(kind) << rect << "sampleChars=" << sample.size();
}

NSArray<NSRegularExpression *> *apiKeyRegularExpressions()
{
    static NSArray<NSRegularExpression *> *cached = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSArray<NSString *> *patterns = @[
            @"sk-proj-[A-Za-z0-9_-]+",
            @"sk-[A-Za-z0-9]{20,}",
            @"AKIA[0-9A-Z]{16}",
            @"github_pat_[A-Za-z0-9_]{20,}",
            @"ghp_[A-Za-z0-9_]{20,}",
            @"sk_live_[A-Za-z0-9]+",
            @"sk_test_[A-Za-z0-9]+",
            @"pk_live_[A-Za-z0-9]+",
            @"xoxb-[A-Za-z0-9-]+",
            @"xoxp-[A-Za-z0-9-]+",
            @"[A-Za-z0-9_-]{10,}\\.[A-Za-z0-9_-]{10,}\\.[A-Za-z0-9_-]{10,}",
        ];
        NSMutableArray<NSRegularExpression *> *built = [NSMutableArray array];
        for (NSString *pattern in patterns) {
            NSError *regexError = nil;
            NSRegularExpression *regex =
                [NSRegularExpression regularExpressionWithPattern:pattern options:0 error:&regexError];
            if (!regex) {
                qWarning() << "detectSensitive: API key regex failed"
                           << QString::fromNSString(pattern)
                           << (regexError ? QString::fromNSString(regexError.localizedDescription)
                                          : QStringLiteral("nil"));
                continue;
            }
            [built addObject:regex];
            qInfo() << "detectSensitive: compiled API key regex" << QString::fromNSString(pattern);
        }
        cached = [built copy];
    });
    return cached;
}

void collectTextHits(const QList<TextLine> &lines, NSString *joined, int width, int height, int kindMask,
                     QList<SensitiveHit> *hits)
{
    if (!joined || joined.length == 0) {
        qInfo() << "detectSensitive: no OCR text for phones/emails/keys";
        return;
    }
    qInfo() << "detectSensitive: OCR joined chars=" << static_cast<qulonglong>(joined.length)
            << "lines=" << lines.size() << "mask=" << kindMask;

    NSError *detectorError = nil;
    NSTextCheckingTypes types = NSTextCheckingTypePhoneNumber | NSTextCheckingTypeLink;
    NSDataDetector *detector = [NSDataDetector dataDetectorWithTypes:types error:&detectorError];
    if (!detector) {
        qWarning() << "detectSensitive: NSDataDetector failed"
                   << (detectorError ? QString::fromNSString(detectorError.localizedDescription)
                                     : QStringLiteral("nil"));
    } else {
        [detector enumerateMatchesInString:joined
                                   options:0
                                     range:NSMakeRange(0, joined.length)
                                usingBlock:^(NSTextCheckingResult *result, NSMatchingFlags flags, BOOL *stop) {
                                    Q_UNUSED(flags);
                                    Q_UNUSED(stop);
                                    if (!result) {
                                        return;
                                    }
                                    const QString sample = QString::fromNSString(
                                        [joined substringWithRange:result.range]);
                                    const QRect rect = rectForJoinedRange(lines, result.range, width, height);
                                    if (result.resultType == NSTextCheckingTypePhoneNumber
                                        && (kindMask & kSensitiveKindPhone)) {
                                        qInfo() << "detectSensitive: phone match chars=" << sample.size()
                                                << "loc=" << static_cast<qulonglong>(result.range.location);
                                        appendHit(hits, rect, SensitiveKind::Phone, sample);
                                    } else if (result.resultType == NSTextCheckingTypeLink) {
                                        NSURL *url = result.URL;
                                        const QString scheme =
                                            url ? QString::fromNSString(url.scheme).toLower() : QString();
                                        if (scheme == QLatin1String("mailto") && (kindMask & kSensitiveKindEmail)) {
                                            if (!sample.contains(QLatin1Char('@'))) {
                                                qInfo() << "detectSensitive: skip mailto without @ chars="
                                                        << sample.size()
                                                        << "loc=" << static_cast<qulonglong>(result.range.location);
                                            } else {
                                                qInfo() << "detectSensitive: email match chars=" << sample.size()
                                                        << "loc="
                                                        << static_cast<qulonglong>(result.range.location);
                                                appendHit(hits, rect, SensitiveKind::Email, sample);
                                            }
                                        } else {
                                            qInfo() << "detectSensitive: skip link scheme=" << scheme
                                                    << "chars=" << sample.size();
                                        }
                                    } else {
                                        qInfo() << "detectSensitive: skip checking type="
                                                << static_cast<qulonglong>(result.resultType)
                                                << "chars=" << sample.size();
                                    }
                                }];
    }

    if (!(kindMask & kSensitiveKindApiKey)) {
        qInfo() << "detectSensitive: API key mask off";
        return;
    }
    NSArray<NSRegularExpression *> *regexes = apiKeyRegularExpressions();
    for (NSRegularExpression *regex in regexes) {
        [regex enumerateMatchesInString:joined
                                options:0
                                  range:NSMakeRange(0, joined.length)
                             usingBlock:^(NSTextCheckingResult *result, NSMatchingFlags flags, BOOL *stop) {
                                 Q_UNUSED(flags);
                                 Q_UNUSED(stop);
                                 if (!result) {
                                     return;
                                 }
                                 const QString sample =
                                     QString::fromNSString([joined substringWithRange:result.range]);
                                 const QRect rect = rectForJoinedRange(lines, result.range, width, height);
                                 qInfo() << "detectSensitive: API key match chars=" << sample.size()
                                         << "loc=" << static_cast<qulonglong>(result.range.location)
                                         << "pattern=" << QString::fromNSString(regex.pattern);
                                 appendHit(hits, rect, SensitiveKind::ApiKey, sample);
                             }];
    }
}

} // namespace

// ─── Ariadne's Thread [AT-0392] ─────────────────────
// What: Vision face+OCR plus NSDataDetector and API-key regex to pixel boxes
// Why:  Auto Blur must stay on-device with official macOS APIs
// Date: 2026-09-03
// Related: [AT-0391] app→SensitiveRedact.h, [AT-0059] app→PersonCutout.mm:cutOutPerson
// ─────────────────────────────────────────────────────
QList<SensitiveHit> detectSensitive(const QImage &image, int kindMask, QString *errorCode)
{
    QList<SensitiveHit> hits;
    @autoreleasepool {
        if (image.isNull()) {
            qWarning() << "detectSensitive: null image mask=" << kindMask;
            if (errorCode) {
                *errorCode = QStringLiteral("REDACT_FAILED");
            }
            return hits;
        }
        const int width = image.width();
        const int height = image.height();
        qInfo() << "detectSensitive: start size=" << image.size() << "mask=" << kindMask
                << "format=" << static_cast<int>(image.format());
        if (kindMask == 0) {
            qInfo() << "detectSensitive: empty mask, skip Vision";
            return hits;
        }

        CGImageRef cg = cgImageFromQImage(image);
        if (!cg) {
            qWarning() << "detectSensitive: CGImage failed size=" << image.size();
            if (errorCode) {
                *errorCode = QStringLiteral("REDACT_FAILED");
            }
            return hits;
        }

        QList<QRect> faceBoxes;
        if (kindMask & kSensitiveKindFace) {
            // ─── Ariadne's Thread [AT-0408] ─────────────────────
            // What: Fine tiles, Lanczos upsample, full-frame 3x boost, CIDetector min 0.01
            // Why:  Avatar and passport-thumbnail faces were below Vision's 1x size
            // Date: 2026-09-03
            // Related: [AT-0399] SensitiveRedact.mm:collectVisionFaces, Apple CIDetectorMinFeatureSize
            // ─────────────────────────────────────────────────────
            const QList<QRect> tiles = faceSearchTiles(width, height);
            for (const QRect &tile : tiles) {
                collectVisionFaces(cg, tile, width, height, &faceBoxes);
            }
            collectVisionFacesBoosted(cg, width, height, &faceBoxes);
            collectCoreImageFaces(cg, width, height, &faceBoxes);
            qInfo() << "detectSensitive: merged faces=" << faceBoxes.size();
        }

        VNRecognizeTextRequest *textRequest = nil;
        if (kindMask & (kSensitiveKindPhone | kSensitiveKindEmail | kSensitiveKindApiKey)) {
            textRequest = [[VNRecognizeTextRequest alloc] init];
            textRequest.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
            textRequest.usesLanguageCorrection = NO;
            VNImageRequestHandler *handler = [[VNImageRequestHandler alloc] initWithCGImage:cg options:@{}];
            NSError *visionError = nil;
            const BOOL ok = [handler performRequests:@[textRequest] error:&visionError];
            if (!ok) {
                qWarning() << "detectSensitive: Vision OCR failed"
                           << (visionError ? QString::fromNSString(visionError.localizedDescription)
                                           : QStringLiteral("nil"));
                if (errorCode) {
                    *errorCode = QStringLiteral("REDACT_FAILED");
                }
            } else {
                qInfo() << "detectSensitive: OCR request ok";
            }
        }
        CGImageRelease(cg);

        int faceCount = 0;
        for (QRect box : faceBoxes) {
            const int padX = qMax(2, qRound(box.width() * 0.08));
            const int padY = qMax(2, qRound(box.height() * 0.08));
            box = box.adjusted(-padX, -padY, padX, padY).intersected(QRect(0, 0, width, height));
            qInfo() << "detectSensitive: face pad 8% ->" << box;
            appendHit(&hits, box, SensitiveKind::Face, QStringLiteral("face"));
            ++faceCount;
        }

        QList<TextLine> lines;
        NSMutableString *joined = [NSMutableString string];
        if (textRequest) {
            for (VNRecognizedTextObservation *obs in textRequest.results) {
                VNRecognizedText *best = [[obs topCandidates:1] firstObject];
                if (!best || best.string.length == 0) {
                    qInfo() << "detectSensitive: skip empty text observation";
                    continue;
                }
                if (joined.length > 0) {
                    [joined appendString:@"\n"];
                }
                TextLine line;
                line.observation = obs;
                line.text = best;
                line.start = joined.length;
                line.length = best.string.length;
                [joined appendString:best.string];
                lines.append(line);
                qInfo() << "detectSensitive: OCR line start=" << static_cast<qulonglong>(line.start)
                        << "chars=" << static_cast<qulonglong>(line.length)
                        << "confidence=" << obs.confidence;
            }
            collectTextHits(lines, joined, width, height, kindMask, &hits);
        }

        int phones = 0;
        int emails = 0;
        int keys = 0;
        for (const SensitiveHit &hit : hits) {
            if (hit.kind == SensitiveKind::Phone) {
                ++phones;
            } else if (hit.kind == SensitiveKind::Email) {
                ++emails;
            } else if (hit.kind == SensitiveKind::ApiKey) {
                ++keys;
            }
        }
        qInfo() << "detectSensitive: done faces=" << faceCount << "phones=" << phones << "emails=" << emails
                << "apiKeys=" << keys << "hits=" << hits.size();
    }
    return hits;
}
