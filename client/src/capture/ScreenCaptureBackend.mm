#include "capture/ScreenCaptureBackend.h"

#include "app/MacPermissions.h"

#include <QDebug>
#include <QEventLoop>
#include <QGuiApplication>
#include <QMetaObject>
#include <QScreen>
#include <QTimer>
#include <atomic>
#include <memory>

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

namespace {

bool isNearlyBlack(const QImage &image)
{
    if (image.isNull() || image.width() == 0 || image.height() == 0) {
        return true;
    }
    const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    qint64 sum = 0;
    const int stepX = qMax(1, rgb.width() / 64);
    const int stepY = qMax(1, rgb.height() / 64);
    int samples = 0;
    for (int y = 0; y < rgb.height(); y += stepY) {
        const uchar *line = rgb.constScanLine(y);
        for (int x = 0; x < rgb.width(); x += stepX) {
            const int i = x * 3;
            sum += line[i] + line[i + 1] + line[i + 2];
            ++samples;
        }
    }
    if (samples == 0) {
        return true;
    }
    const double avg = static_cast<double>(sum) / static_cast<double>(samples * 3);
    qInfo() << "ScreenCaptureBackend: luminance samples=" << samples << " avg=" << avg;
    return avg < 4.0;
}

QImage imageFromCg(CGImageRef cgImage)
{
    if (!cgImage) {
        return {};
    }
    const size_t width = CGImageGetWidth(cgImage);
    const size_t height = CGImageGetHeight(cgImage);
    QImage image(static_cast<int>(width), static_cast<int>(height), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    CGColorSpaceRef colorSpace = CGImageGetColorSpace(cgImage);
    if (colorSpace) {
        CGColorSpaceRetain(colorSpace);
    } else {
        colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    }
    CGContextRef ctx = CGBitmapContextCreate(image.bits(), width, height, 8, image.bytesPerLine(),
                                             colorSpace,
                                             kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
    CGColorSpaceRelease(colorSpace);
    if (!ctx) {
        qWarning() << "ScreenCaptureBackend: CGBitmapContextCreate failed";
        return {};
    }
    CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);
    CGContextDrawImage(ctx, CGRectMake(0, 0, static_cast<CGFloat>(width), static_cast<CGFloat>(height)),
                       cgImage);
    CGContextRelease(ctx);
    qInfo() << "ScreenCaptureBackend: imageFromCg" << (int)width << "x" << (int)height
            << "bpc=" << (int)CGImageGetBitsPerComponent(cgImage)
            << "bpp=" << (int)CGImageGetBitsPerPixel(cgImage);
    return image;
}

SCDisplay *displayForRect(NSArray<SCDisplay *> *displays, const QRect &screenRect)
{
    const QPoint center = screenRect.center();
    for (SCDisplay *display in displays) {
        const CGRect frame = display.frame;
        const QRect qframe(static_cast<int>(frame.origin.x), static_cast<int>(frame.origin.y),
                           static_cast<int>(frame.size.width), static_cast<int>(frame.size.height));
        if (qframe.contains(center)) {
            qInfo() << "ScreenCaptureBackend: matched display" << display.displayID << qframe;
            return display;
        }
    }
    return displays.firstObject;
}

NSArray<SCRunningApplication *> *seenShotApps(SCShareableContent *content)
{
    NSString *bundleId = [[NSBundle mainBundle] bundleIdentifier];
    NSMutableArray<SCRunningApplication *> *apps = [NSMutableArray array];
    if (bundleId.length == 0) {
        qWarning() << "ScreenCaptureBackend: main bundle id empty";
        return apps;
    }
    for (SCRunningApplication *app in content.applications) {
        if ([app.bundleIdentifier isEqualToString:bundleId]) {
            [apps addObject:app];
        }
    }
    qInfo() << "ScreenCaptureBackend: exclude apps=" << (int)apps.count << " bundle=" << QString::fromNSString(bundleId);
    return apps;
}

// ─── Ariadne's Thread [AT-0069] ─────────────────────
// What: Capture at SCCaptureResolutionBest and native pixel size; exclude SeenShot
// Why:  Default config is 1920x1080 Automatic; picker overlay was in the frame
// Date: 2026-08-25
// Related: [AT-0037] ScreenCaptureBackend.mm:captureRegion, [AT-0009] ScreenCaptureBackend.h
// ─────────────────────────────────────────────────────
void captureWithManager(SCShareableContent *content, SCDisplay *display, const QRect &screenRect,
                        void (^done)(QImage image, QString code))
{
    if (@available(macOS 14.0, *)) {
        SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:display
                                                    excludingApplications:seenShotApps(content)
                                                          exceptingWindows:@[]];
        SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
        const CGRect displayFrame = display.frame;
        const CGFloat scale =
            display.frame.size.width > 0 ? (CGFloat)display.width / display.frame.size.width : 1.0;
        CGRect source = CGRectMake(screenRect.x() - displayFrame.origin.x,
                                   screenRect.y() - displayFrame.origin.y, screenRect.width(),
                                   screenRect.height());
        source = CGRectIntersection(source, CGRectMake(0, 0, displayFrame.size.width, displayFrame.size.height));
        const size_t outW = static_cast<size_t>(llround(source.size.width * scale));
        const size_t outH = static_cast<size_t>(llround(source.size.height * scale));
        config.sourceRect = source;
        config.width = outW;
        config.height = outH;
        config.destinationRect = CGRectMake(0, 0, static_cast<CGFloat>(outW), static_cast<CGFloat>(outH));
        config.showsCursor = NO;
        config.scalesToFit = NO;
        config.preservesAspectRatio = YES;
        config.pixelFormat = kCVPixelFormatType_32BGRA;
        config.captureResolution = SCCaptureResolutionBest;
        config.shouldBeOpaque = YES;
        config.colorSpaceName = kCGColorSpaceSRGB;
        qInfo() << "ScreenCaptureBackend: SCScreenshotManager source=" << source.origin.x << source.origin.y
                << source.size.width << source.size.height << " scale=" << scale
                << " out=" << (int)outW << "x" << (int)outH
                << " displayPx=" << (int)display.width << "x" << (int)display.height
                << " mainThread=" << [NSThread isMainThread];

        [SCScreenshotManager captureImageWithFilter:filter
                                      configuration:config
                                  completionHandler:^(CGImageRef _Nullable image, NSError *_Nullable error) {
                                      qInfo() << "ScreenCaptureBackend: captureImage completion"
                                              << " hasImage=" << (image != nil)
                                              << " cg=" << (image ? (int)CGImageGetWidth(image) : 0) << "x"
                                              << (image ? (int)CGImageGetHeight(image) : 0)
                                              << " error=" << (error ? QString::fromNSString(error.localizedDescription) : QString())
                                              << " mainThread=" << [NSThread isMainThread];
                                      if (error) {
                                          done(QImage(), QStringLiteral("SCREEN_RECORDING_DENIED"));
                                          return;
                                      }
                                      done(imageFromCg(image), QString());
                                  }];
        return;
    }
    qWarning() << "ScreenCaptureBackend: SCScreenshotManager unavailable";
    done(QImage(), QStringLiteral("SCREEN_CAPTURE_BLOCKED"));
}

} // namespace

// ─── Ariadne's Thread [AT-0037] ─────────────────────
// What: Wait for ScreenCaptureKit with QEventLoop, not nested semaphores
// Why:  lldb+logs: TCC already YES, but 8s empty-image DENIED — wait on main blocked the completion
// Date: 2026-08-25
// Related: [AT-0009] ScreenCaptureBackend.mm:captureRegion, [AT-0035] MacPermissions.mm
// ─────────────────────────────────────────────────────
QImage ScreenCaptureBackend::captureRegion(const QRect &screenRect, QString *errorCode)
{
    qInfo() << "ScreenCaptureBackend: captureRegion" << screenRect
            << " mainThread=" << [NSThread isMainThread]
            << " preflight=" << MacPermissions::hasScreenRecording();
    [NSApp activateIgnoringOtherApps:YES];

    struct CaptureState {
        QEventLoop *loop = nullptr;
        QImage image;
        QString code;
        std::atomic<bool> done{false};
    };
    const auto state = std::make_shared<CaptureState>();
    QEventLoop loop;
    state->loop = &loop;
    const auto finish = [state]() {
        if (state->done.exchange(true)) {
            return;
        }
        if (state->loop) {
            QMetaObject::invokeMethod(state->loop, "quit", Qt::QueuedConnection);
        }
    };

    [SCShareableContent getShareableContentWithCompletionHandler:^(
                            SCShareableContent *_Nullable content, NSError *_Nullable error) {
        qInfo() << "ScreenCaptureBackend: getShareableContent"
                << " displays=" << (content ? (int)content.displays.count : -1)
                << " error=" << (error ? QString::fromNSString(error.localizedDescription) : QString())
                << " mainThread=" << [NSThread isMainThread];
        if (error || !content) {
            qWarning() << "ScreenCaptureBackend: getShareableContent failed"
                       << (error ? QString::fromNSString(error.localizedDescription) : QStringLiteral("nil content"));
            state->code = QStringLiteral("SCREEN_RECORDING_DENIED");
            finish();
            return;
        }
        SCDisplay *display = displayForRect(content.displays, screenRect);
        if (!display) {
            qWarning() << "ScreenCaptureBackend: no display for rect";
            state->code = QStringLiteral("SCREEN_CAPTURE_BLOCKED");
            finish();
            return;
        }
        captureWithManager(content, display, screenRect, ^(QImage captured, QString captureCode) {
            state->image = captured;
            state->code = captureCode;
            finish();
        });
    }];

    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();
    state->loop = nullptr;
    if (!state->done.exchange(true)) {
        qWarning() << "ScreenCaptureBackend: capture timed out after 10s";
        if (state->code.isEmpty()) {
            state->code = QStringLiteral("SCREEN_RECORDING_DENIED");
        }
    }

    const QImage image = state->image;
    const QString code = state->code;

    if (image.isNull()) {
        if (errorCode) {
            *errorCode = code.isEmpty() ? QStringLiteral("SCREEN_RECORDING_DENIED") : code;
        }
        qWarning() << "ScreenCaptureBackend: empty image code=" << (errorCode ? *errorCode : code);
        return {};
    }
    if (isNearlyBlack(image)) {
        qWarning() << "ScreenCaptureBackend: frame is nearly black (DRM or denied)";
        if (errorCode) {
            *errorCode = QStringLiteral("SCREEN_CAPTURE_BLOCKED");
        }
        return {};
    }
    qInfo() << "ScreenCaptureBackend: captured" << image.width() << "x" << image.height();
    return image;
}
