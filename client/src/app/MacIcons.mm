#include "app/MacIcons.h"

#include <QByteArray>
#include <QDebug>
#include <QPixmap>

#import <AppKit/AppKit.h>

// ─── Ariadne's Thread [AT-0055] ─────────────────────
// What: Rasterize SF Symbol via NSBitmapImageRep PNG, tint with labelColor
// Why:  NSImage TIFFRepresentation of system symbols is not a Qt-readable TIFF; icons stayed null
// Date: 2026-08-25
// Related: [AT-0053] MacIcons.h:macToolbarIcon, [AT-0012] AnnotateWindow.cpp
// ─────────────────────────────────────────────────────
QIcon macToolbarIcon(const QString &symbolName)
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
        [NSImageSymbolConfiguration configurationWithPointSize:14.0
                                                        weight:NSFontWeightRegular
                                                         scale:NSImageSymbolScaleMedium];
    NSImage *configured = [symbol imageWithSymbolConfiguration:config];
    if (configured) {
        symbol = configured;
    } else {
        qWarning() << "macToolbarIcon: symbol configuration failed" << symbolName;
    }

    const CGFloat scale = [NSScreen mainScreen] ? [NSScreen mainScreen].backingScaleFactor : 1.0;
    const NSInteger px = (NSInteger)llround(16.0 * (scale > 0 ? scale : 1.0));
    qInfo() << "macToolbarIcon: rasterize" << symbolName << "px=" << (int)px << "scale=" << scale;

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
    const NSRect dest = NSMakeRect(0, 0, px, px);
    [[NSColor clearColor] set];
    NSRectFill(dest);
    [symbol setTemplate:YES];
    [symbol drawInRect:dest
              fromRect:NSZeroRect
             operation:NSCompositingOperationSourceOver
              fraction:1.0
        respectFlipped:YES
                 hints:nil];
    [[NSColor labelColor] set];
    NSRectFillUsingOperation(dest, NSCompositingOperationSourceIn);
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
            << "dpr=" << pix.devicePixelRatio() << "null=" << pix.isNull();
    return QIcon(pix);
}
