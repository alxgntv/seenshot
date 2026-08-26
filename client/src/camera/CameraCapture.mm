#include "camera/CameraCapture.h"

#include <QByteArray>
#include <QDebug>
#include <QMetaObject>
#include <QPointer>
#include <QWidget>

#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

@interface SeenShotPhotoDelegate : NSObject <AVCapturePhotoCaptureDelegate>
@property (nonatomic, copy) void (^handler)(NSData *data, NSError *error);
@end

@implementation SeenShotPhotoDelegate
- (void)captureOutput:(AVCapturePhotoOutput *)output
    didFinishProcessingPhoto:(AVCapturePhoto *)photo
                       error:(NSError *)error
{
    (void)output;
    if (error) {
        qWarning() << "CameraCapture: photo error" << QString::fromNSString(error.localizedDescription);
        if (self.handler) {
            self.handler(nil, error);
        }
        return;
    }
    NSData *data = [photo fileDataRepresentation];
    qInfo() << "CameraCapture: photo bytes=" << (int)data.length;
    if (self.handler) {
        self.handler(data, nil);
    }
}
@end

CameraCapture::CameraCapture(QObject *parent)
    : QObject(parent)
{
    qInfo() << "CameraCapture: constructed";
}

CameraCapture::~CameraCapture()
{
    stop();
    qInfo() << "CameraCapture: destroyed";
}

bool CameraCapture::isRunning() const
{
    return m_session != nullptr;
}

bool CameraCapture::startPreview(QWidget *host, QString *errorCode)
{
    stop();
    if (!host) {
        qWarning() << "CameraCapture: startPreview null host";
        if (errorCode) {
            *errorCode = QStringLiteral("CAMERA_UNAVAILABLE");
        }
        return false;
    }
    AVCaptureDevice *device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
    if (!device) {
        qWarning() << "CameraCapture: no default video device";
        if (errorCode) {
            *errorCode = QStringLiteral("CAMERA_UNAVAILABLE");
        }
        return false;
    }
    NSError *error = nil;
    AVCaptureDeviceInput *input = [AVCaptureDeviceInput deviceInputWithDevice:device error:&error];
    if (!input) {
        qWarning() << "CameraCapture: input failed" << QString::fromNSString(error.localizedDescription);
        if (errorCode) {
            *errorCode = QStringLiteral("CAMERA_UNAVAILABLE");
        }
        return false;
    }
    AVCaptureSession *session = [[AVCaptureSession alloc] init];
    session.sessionPreset = AVCaptureSessionPresetPhoto;
    if (![session canAddInput:input]) {
        qWarning() << "CameraCapture: cannot add input";
        if (errorCode) {
            *errorCode = QStringLiteral("CAMERA_UNAVAILABLE");
        }
        return false;
    }
    [session addInput:input];
    AVCapturePhotoOutput *photoOutput = [[AVCapturePhotoOutput alloc] init];
    if (![session canAddOutput:photoOutput]) {
        qWarning() << "CameraCapture: cannot add photo output";
        if (errorCode) {
            *errorCode = QStringLiteral("PHOTO_CAPTURE_FAILED");
        }
        return false;
    }
    [session addOutput:photoOutput];

    if (!host->isWindow()) {
        qWarning() << "CameraCapture: startPreview host is not a top-level window";
        if (errorCode) {
            *errorCode = QStringLiteral("CAMERA_UNAVAILABLE");
        }
        return false;
    }
    host->setAutoFillBackground(false);
    host->winId();
    NSView *nsView = (__bridge NSView *)reinterpret_cast<void *>(host->winId());
    nsView.wantsLayer = YES;
    AVCaptureVideoPreviewLayer *layer = [AVCaptureVideoPreviewLayer layerWithSession:session];
    layer.videoGravity = AVLayerVideoGravityResizeAspectFill;
    layer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
    AVCaptureConnection *connection = layer.connection;
    if (connection.isVideoMirroringSupported) {
        connection.automaticallyAdjustsVideoMirroring = NO;
        connection.videoMirrored = YES;
        qInfo() << "CameraCapture: preview mirrored";
    }
    // ─── Ariadne's Thread [AT-0080] ─────────────────────
    // What: Host must already be a top-level window; preview is a sublayer only
    // Why:  WA_NativeWindow on a viewport child freed QContainerLayer → SIGSEGV
    // Date: 2026-08-25
    // Related: [AT-0079] CameraCapture.mm:startPreview, [AT-0080] AnnotateWindow.cpp
    // ─────────────────────────────────────────────────────
    [nsView.layer addSublayer:layer];
    layer.frame = nsView.bounds;
    qInfo() << "CameraCapture: preview added as sublayer bounds=" << nsView.bounds.size.width << "x"
            << nsView.bounds.size.height << " qtLayer=" << (nsView.layer != nil);
    [session startRunning];

    m_session = (__bridge_retained void *)session;
    m_previewLayer = (__bridge_retained void *)layer;
    m_photoOutput = (__bridge_retained void *)photoOutput;
    qInfo() << "CameraCapture: preview started device=" << QString::fromNSString(device.localizedName)
            << " host=" << host->size() << " nsBounds=" << nsView.bounds.size.width << "x"
            << nsView.bounds.size.height;
    syncPreviewLayer();
    return true;
}

// ─── Ariadne's Thread [AT-0074] ─────────────────────
// What: Preview layer is the host NSView.layer; frame follows that view's bounds
// Why:  addSublayer sat under Qt's opaque fill; superlayer.bounds sized the pip wrong
// Date: 2026-08-25
// Related: [AT-0068] CameraCapture.mm:syncPreviewLayer, [AT-0074] AnnotateWindow.cpp
// ─────────────────────────────────────────────────────
void CameraCapture::syncPreviewLayer()
{
    if (!m_previewLayer) {
        qWarning() << "CameraCapture: syncPreviewLayer no layer";
        return;
    }
    AVCaptureVideoPreviewLayer *layer = (__bridge AVCaptureVideoPreviewLayer *)m_previewLayer;
    CALayer *superlayer = layer.superlayer;
    if (!superlayer) {
        qWarning() << "CameraCapture: syncPreviewLayer no superlayer";
        return;
    }
    layer.frame = superlayer.bounds;
    qInfo() << "CameraCapture: preview layer frame=" << layer.frame.size.width << "x"
            << layer.frame.size.height;
}

void CameraCapture::captureStill()
{
    if (!m_photoOutput) {
        qWarning() << "CameraCapture: captureStill without output";
        emit stillFailed(QStringLiteral("PHOTO_CAPTURE_FAILED"));
        return;
    }
    AVCapturePhotoOutput *output = (__bridge AVCapturePhotoOutput *)m_photoOutput;
    SeenShotPhotoDelegate *delegate = [[SeenShotPhotoDelegate alloc] init];
    QPointer<CameraCapture> self(this);
    delegate.handler = ^(NSData *data, NSError *error) {
        QMetaObject::invokeMethod(
            self, [self, data, error]() {
                if (!self) {
                    qWarning() << "CameraCapture: still callback after destroy";
                    return;
                }
                if (error || !data) {
                    emit self->stillFailed(QStringLiteral("PHOTO_CAPTURE_FAILED"));
                    return;
                }
                QImage image;
                if (!image.loadFromData(QByteArray::fromNSData(data))) {
                    qWarning() << "CameraCapture: JPEG load failed bytes=" << (int)data.length;
                    emit self->stillFailed(QStringLiteral("PHOTO_CAPTURE_FAILED"));
                    return;
                }
                const QImage mirrored = image.flipped(Qt::Horizontal).convertToFormat(QImage::Format_ARGB32);
                qInfo() << "CameraCapture: still ready" << mirrored.size();
                emit self->stillReady(mirrored);
            },
            Qt::QueuedConnection);
    };
    m_delegate = (__bridge_retained void *)delegate;
    AVCapturePhotoSettings *settings = [AVCapturePhotoSettings photoSettings];
    qInfo() << "CameraCapture: captureStill";
    [output capturePhotoWithSettings:settings delegate:delegate];
}

void CameraCapture::stop()
{
    if (m_previewLayer) {
        AVCaptureVideoPreviewLayer *layer = (__bridge_transfer AVCaptureVideoPreviewLayer *)m_previewLayer;
        [layer removeFromSuperlayer];
        m_previewLayer = nullptr;
    }
    if (m_session) {
        AVCaptureSession *session = (__bridge_transfer AVCaptureSession *)m_session;
        if (session.isRunning) {
            [session stopRunning];
        }
        m_session = nullptr;
        qInfo() << "CameraCapture: session stopped";
    }
    if (m_photoOutput) {
        (void)(__bridge_transfer AVCapturePhotoOutput *)m_photoOutput;
        m_photoOutput = nullptr;
    }
    if (m_delegate) {
        (void)(__bridge_transfer SeenShotPhotoDelegate *)m_delegate;
        m_delegate = nullptr;
    }
}
