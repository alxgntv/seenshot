#include "app/MacIcons.h"

#include <QByteArray>
#include <QDebug>
#include <QPixmap>

#import <AppKit/AppKit.h>

QIcon macToolbarIcon(const QString &symbolName, const QColor &tint)
{
    if (symbolName.isEmpty()) {
        qWarning() << "macToolbarIcon: empty SF Symbol name";
        return QIcon();
    }
    NSImage *symbol = [NSImage imageWithSystemSymbolName:symbolName.toNSString()
                                accessibilityDescription:nil];
    if (!symbol) {
        qWarning() << "macToolbarIcon: missing SF Symbol" << symbolName;
        return QIcon();
    }
    NSImageSymbolConfiguration *config =
        [NSImageSymbolConfiguration configurationWithPointSize:18.0
                                                        weight:NSFontWeightRegular
                                                         scale:NSImageSymbolScaleMedium];
    NSImage *configured = [symbol imageWithSymbolConfiguration:config];
    if (configured) {
        symbol = configured;
    } else {
        qWarning() << "macToolbarIcon: symbol configuration failed" << symbolName;
    }

    const CGFloat scale = [NSScreen mainScreen] ? [NSScreen mainScreen].backingScaleFactor : 1.0;
    const NSInteger px = (NSInteger)llround(22.0 * (scale > 0 ? scale : 1.0));
    qInfo() << "macToolbarIcon: rasterize" << symbolName << "px=" << (int)px << "scale=" << scale
            << "tintValid=" << tint.isValid();

    NSBitmapImageRep *rep = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:nil
                      pixelsWide:px
                      pixelsHigh:px
                   bitsPerSample:8
                 samplesPerPixel:4
                        hasAlpha:YES
                        isPlanar:NO
                  colorSpaceName:NSDeviceRGBColorSpace
                     bytesPerRow:0
                    bitsPerPixel:0];
    if (!rep) {
        qWarning() << "macToolbarIcon: NSBitmapImageRep failed" << symbolName;
        return QIcon();
    }

    [NSGraphicsContext saveGraphicsState];
    NSGraphicsContext *gc = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
    if (!gc) {
        [NSGraphicsContext restoreGraphicsState];
        qWarning() << "macToolbarIcon: graphics context failed" << symbolName;
        return QIcon();
    }
    [NSGraphicsContext setCurrentContext:gc];
    const NSRect canvas = NSMakeRect(0, 0, px, px);
    [[NSColor clearColor] set];
    NSRectFill(canvas);
    [symbol setTemplate:YES];
    NSSize nat = symbol.size;
    if (nat.width < 1.0 || nat.height < 1.0) {
        nat = NSMakeSize(px, px);
    }
    const CGFloat fit = MIN(canvas.size.width / nat.width, canvas.size.height / nat.height);
    const NSSize draw = NSMakeSize(nat.width * fit, nat.height * fit);
    const NSRect dest = NSMakeRect((canvas.size.width - draw.width) * 0.5,
                                   (canvas.size.height - draw.height) * 0.5, draw.width, draw.height);
    [symbol drawInRect:dest
              fromRect:NSZeroRect
             operation:NSCompositingOperationSourceOver
              fraction:1.0
        respectFlipped:YES
                 hints:nil];
    NSColor *fill = nil;
    if (tint.isValid()) {
        fill = [NSColor colorWithSRGBRed:tint.redF() green:tint.greenF() blue:tint.blueF() alpha:tint.alphaF()];
    } else {
        fill = [NSColor labelColor];
    }
    [fill set];
    NSRectFillUsingOperation(canvas, NSCompositingOperationSourceIn);
    [NSGraphicsContext restoreGraphicsState];

    NSData *png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    if (!png) {
        qWarning() << "macToolbarIcon: PNG encode failed" << symbolName;
        return QIcon();
    }
    QPixmap pix;
    if (!pix.loadFromData(QByteArray::fromNSData(png), "PNG")) {
        qWarning() << "macToolbarIcon: PNG load failed" << symbolName << "bytes=" << (int)png.length;
        return QIcon();
    }
    pix.setDevicePixelRatio(scale > 0 ? scale : 1.0);
    qInfo() << "macToolbarIcon: loaded" << symbolName << "size=" << pix.size()
            << "dpr=" << pix.devicePixelRatio() << "dest=" << dest.size.width << "x" << dest.size.height;
    return QIcon(pix);
}

// ─── Ariadne's Thread [AT-0305] ─────────────────────
// What: Rasterize NSWorkspace iconForFile of the running .app
// Why:  QIcon does not load SeenShot.icns; Welcome page needs the bundle mark
// Date: 2026-08-28
// Related: [AT-0302] FirstRunWizard.cpp, [AT-0055] MacIcons.mm:macToolbarIcon
// ─────────────────────────────────────────────────────
QPixmap macBundleIcon(int pointSize)
{
    if (pointSize < 1) {
        qWarning() << "macBundleIcon: invalid pointSize=" << pointSize;
        return {};
    }
    NSString *path = [NSBundle mainBundle].bundlePath ?: @"";
    NSImage *image = [[NSWorkspace sharedWorkspace] iconForFile:path];
    if (!image) {
        qWarning() << "macBundleIcon: iconForFile nil path=" << QString::fromNSString(path);
        return {};
    }
    const CGFloat scale = [NSScreen mainScreen] ? [NSScreen mainScreen].backingScaleFactor : 1.0;
    const NSInteger px = (NSInteger)llround((CGFloat)pointSize * (scale > 0 ? scale : 1.0));
    qInfo() << "macBundleIcon: rasterize pointSize=" << pointSize << " px=" << static_cast<int>(px)
            << " scale=" << scale << " path=" << QString::fromNSString(path);
    NSBitmapImageRep *rep = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:nil
                      pixelsWide:px
                      pixelsHigh:px
                   bitsPerSample:8
                 samplesPerPixel:4
                        hasAlpha:YES
                        isPlanar:NO
                  colorSpaceName:NSDeviceRGBColorSpace
                     bytesPerRow:0
                    bitsPerPixel:0];
    if (!rep) {
        qWarning() << "macBundleIcon: NSBitmapImageRep failed";
        return {};
    }
    [NSGraphicsContext saveGraphicsState];
    NSGraphicsContext *gc = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
    if (!gc) {
        [NSGraphicsContext restoreGraphicsState];
        qWarning() << "macBundleIcon: graphics context failed";
        return {};
    }
    [NSGraphicsContext setCurrentContext:gc];
    const NSRect canvas = NSMakeRect(0, 0, px, px);
    [[NSColor clearColor] set];
    NSRectFill(canvas);
    [image drawInRect:canvas
              fromRect:NSZeroRect
             operation:NSCompositingOperationSourceOver
              fraction:1.0
        respectFlipped:YES
                 hints:nil];
    [NSGraphicsContext restoreGraphicsState];
    NSData *png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    if (!png) {
        qWarning() << "macBundleIcon: PNG encode failed";
        return {};
    }
    QPixmap pix;
    if (!pix.loadFromData(QByteArray::fromNSData(png), "PNG")) {
        qWarning() << "macBundleIcon: PNG load failed bytes=" << static_cast<int>(png.length);
        return {};
    }
    pix.setDevicePixelRatio(scale > 0 ? scale : 1.0);
    qInfo() << "macBundleIcon: loaded size=" << pix.size() << " dpr=" << pix.devicePixelRatio();
    return pix;
}
