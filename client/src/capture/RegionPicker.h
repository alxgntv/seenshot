#pragma once

#include <QImage>
#include <QList>
#include <QPoint>
#include <QRect>
#include <QWidget>

class QMoveEvent;
class QPainter;

// ─── Ariadne's Thread [AT-0010] ─────────────────────
// What: Fullscreen drag overlay to pick a capture region
// Why:  Region mode from the product spec
// Date: 2026-08-25
// Related: client/src/capture/RegionPicker.cpp
// ─────────────────────────────────────────────────────
// ─── Ariadne's Thread [AT-0409] ─────────────────────
// What: Frozen screen frames for path capture crop
// Why:  Clicking the overlay dismissed menus before ScreenCaptureKit ran
// Date: 2026-09-04
// Related: [AT-0010] RegionPicker.cpp, [AT-0406] ScreenCaptureBackend.mm:captureRegion
// ─────────────────────────────────────────────────────
struct CaptureFreezeFrame {
    QRect geometry;
    QImage image;
};

class RegionPicker : public QWidget {
    Q_OBJECT
public:
    explicit RegionPicker(const QList<CaptureFreezeFrame> &frames, QWidget *parent = nullptr);
    // ─── Ariadne's Thread [AT-0403] ─────────────────────
    // What: Yield and restore overlay keyboard grab around a warning dialog
    // Why:  Path overlay grabKeyboard ate keys for QMessageBox under the shield
    // Date: 2026-09-03
    // Related: [AT-0401] MacPermissions.mm:pinAlertAboveCapture, [AT-0117] RegionPicker.cpp:showEvent
    // ─────────────────────────────────────────────────────
    void yieldInput();
    void resumeInput();
    QImage croppedFreeze(const QRect &globalRect) const;

signals:
    void regionPicked(const QRect &screenRect);
    void cancelled();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void moveEvent(QMoveEvent *event) override;

private:
    void installEscapeMonitor();
    void syncRubberFromGlobal(const QPoint &globalNow);
    void drawFreeze(QPainter &painter, const QRect &clip) const;
    QRect screenUnion() const;

    QList<CaptureFreezeFrame> m_frames;
    QPoint m_originGlobal;
    QRect m_rubber;
    bool m_dragging = false;
    bool m_snapping = false;
};
