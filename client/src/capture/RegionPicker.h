#pragma once

#include <QPoint>
#include <QRect>
#include <QWidget>

class QMoveEvent;

// ─── Ariadne's Thread [AT-0010] ─────────────────────
// What: Fullscreen drag overlay to pick a capture region
// Why:  Region mode from the product spec
// Date: 2026-08-25
// Related: client/src/capture/RegionPicker.cpp
// ─────────────────────────────────────────────────────
class RegionPicker : public QWidget {
    Q_OBJECT
public:
    explicit RegionPicker(QWidget *parent = nullptr);

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
    void syncRubberFromGlobal(const QPoint &globalNow);
    QRect screenUnion() const;

    QPoint m_originGlobal;
    QRect m_rubber;
    bool m_dragging = false;
    bool m_snapping = false;
};
