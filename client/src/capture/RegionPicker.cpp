#include "capture/RegionPicker.h"

#include <QDebug>
#include <QGuiApplication>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPainter>
#include <QScreen>
#include <QShowEvent>

RegionPicker::RegionPicker(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setAttribute(Qt::WA_QuitOnClose, false);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setGeometry(screenUnion());
    qInfo() << "RegionPicker: overlay geometry" << geometry() << " screens=" << QGuiApplication::screens().size()
            << " alwaysShowTool=" << testAttribute(Qt::WA_MacAlwaysShowToolWindow)
            << " quitOnClose=" << testAttribute(Qt::WA_QuitOnClose);
}

QRect RegionPicker::screenUnion() const
{
    QRect unionRect;
    const auto screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        unionRect = unionRect.united(screen->geometry());
    }
    return unionRect;
}

void RegionPicker::syncRubberFromGlobal(const QPoint &globalNow)
{
    const QPoint a = mapFromGlobal(m_originGlobal);
    const QPoint b = mapFromGlobal(globalNow);
    m_rubber = QRect(a, b).normalized();
    qInfo() << "RegionPicker: rubber local=" << m_rubber << " originGlobal=" << m_originGlobal
            << " nowGlobal=" << globalNow << " geo=" << geometry();
}

// ─── Ariadne's Thread [AT-0117] ─────────────────────
// What: Take keyboard so Escape can cancel a stuck overlay
// Why:  Tool overlay was not key; path hotkey left capturing=true
// Date: 2026-08-26
// Related: [AT-0118] MacPermissions.mm:pinCaptureOverlay, [AT-0010] RegionPicker.cpp
// ─────────────────────────────────────────────────────
void RegionPicker::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    setFocus(Qt::ActiveWindowFocusReason);
    grabKeyboard();
    qInfo() << "RegionPicker: showEvent visible=" << isVisible() << " geo=" << geometry()
            << " active=" << isActiveWindow();
}

void RegionPicker::hideEvent(QHideEvent *event)
{
    if (mouseGrabber() == this) {
        releaseMouse();
        qInfo() << "RegionPicker: hideEvent releaseMouse";
    }
    releaseKeyboard();
    QWidget::hideEvent(event);
    qInfo() << "RegionPicker: hideEvent";
}

// ─── Ariadne's Thread [AT-0164] ─────────────────────
// What: Snap RegionPicker back if macOS drags the overlay from the first pixels
// Why:  press (28,5) then release QRect(28,-66) meant the window moved and captured 25x71
// Date: 2026-08-26
// Related: [AT-0163] MacPermissions.mm:pinCaptureOverlay, [AT-0010] RegionPicker.cpp
// ─────────────────────────────────────────────────────
void RegionPicker::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    if (m_snapping) {
        qInfo() << "RegionPicker: moveEvent skip snap old=" << event->oldPos() << " pos=" << event->pos();
        return;
    }
    const QRect lock = screenUnion();
    if (geometry() == lock) {
        qInfo() << "RegionPicker: moveEvent already locked geo=" << geometry();
        return;
    }
    qWarning() << "RegionPicker: window moved old=" << event->oldPos() << " pos=" << event->pos()
               << " geo=" << geometry() << " snap=" << lock;
    m_snapping = true;
    setGeometry(lock);
    m_snapping = false;
}

void RegionPicker::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 90));
    if (!m_rubber.isNull()) {
        painter.setCompositionMode(QPainter::CompositionMode_Clear);
        painter.fillRect(m_rubber, Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.setPen(QPen(Qt::white, 1));
        painter.drawRect(m_rubber.adjusted(0, 0, -1, -1));
    }
}

void RegionPicker::mousePressEvent(QMouseEvent *event)
{
    m_dragging = true;
    m_originGlobal = event->globalPosition().toPoint();
    grabMouse();
    syncRubberFromGlobal(m_originGlobal);
    qInfo() << "RegionPicker: press local=" << event->pos() << " global=" << m_originGlobal
            << " geo=" << geometry();
    update();
}

void RegionPicker::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging) {
        return;
    }
    syncRubberFromGlobal(event->globalPosition().toPoint());
    update();
}

void RegionPicker::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_dragging) {
        return;
    }
    m_dragging = false;
    if (mouseGrabber() == this) {
        releaseMouse();
    }
    const QPoint now = event->globalPosition().toPoint();
    const QRect global = QRect(m_originGlobal, now).normalized();
    qInfo() << "RegionPicker: release originGlobal=" << m_originGlobal << " now=" << now
            << " global=" << global << " geo=" << geometry();
    hide();
    if (global.width() < 4 || global.height() < 4) {
        qInfo() << "RegionPicker: cancelled tiny global=" << global;
        emit cancelled();
        return;
    }
    emit regionPicked(global);
}

void RegionPicker::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        qInfo() << "RegionPicker: cancelled by Escape";
        hide();
        emit cancelled();
    }
}
