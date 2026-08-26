#pragma once

#include <QImage>
#include <QObject>
#include <QString>

class QWidget;

// ─── Ariadne's Thread [AT-0058] ─────────────────────
// What: AVFoundation still capture with mirrored live preview
// Why:  PRD-03 Photo — selfie preview then one AVCapturePhotoOutput frame
// Date: 2026-08-25
// Related: [AT-0059] PersonCutout.h, docs/PRD-03-photo-cutout.md
// ─────────────────────────────────────────────────────
class CameraCapture : public QObject {
    Q_OBJECT
public:
    explicit CameraCapture(QObject *parent = nullptr);
    ~CameraCapture() override;

    bool startPreview(QWidget *host, QString *errorCode);
    void syncPreviewLayer();
    void setPreviewSelected(bool selected);
    void captureStill();
    void stop();
    bool isRunning() const;

signals:
    void stillReady(const QImage &image);
    void stillFailed(const QString &code);

private:
    void *m_session = nullptr;
    void *m_previewLayer = nullptr;
    void *m_photoOutput = nullptr;
    void *m_delegate = nullptr;
    bool m_previewSelected = false;
};
